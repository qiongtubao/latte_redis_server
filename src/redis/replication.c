/**
 * replication.c - 主从复制核心实现
 *
 * 功能概述：
 *   - slave 侧：SLAVEOF host port → 连接 master → PING → SYNC → 接收全量 LDB 数据
 *               → 本地 load → 进入增量复制（接收 master backlog 命令并在本地执行）
 *   - master 侧：接收 slave 的 SYNC 请求 → 序列化全量数据 → 发送给 slave
 *               → 定时推送 backlog 增量命令给所有 slave
 *
 * 协议设计（简化版，非标准 Redis PSYNC 协议）：
 *   1. slave 连接 master 后发送 "PING\r\n"
 *   2. master 回复 "+PONG\r\n"
 *   3. slave 发送 "SYNC\r\n"
 *   4. master 回复 "$<len>\r\n<LDB-binary-data><len-bytes>\r\n"（bulk string 格式）
 *   5. slave 接收完整 LDB 数据后 load 到本地 DB
 *   6. 之后 master 持续发送 backlog 增量命令（每条用 RESP inline 格式，以 \r\n 分隔）
 */

#include "replication.h"
#include "server.h"
#include "client.h"
#include "db.h"
#include "../shared/shared.h"
#include "../object/string.h"
#include "debug/latte_debug.h"
#include "zmalloc/zmalloc.h"
#include "connection/connection.h"
#include "../../deps/latte_c/src/odb/odb.h"
#include "../../deps/latte_c/src/object/object_manager.h"
#include "../../deps/latte_c/src/dict/dict.h"
#include "../../deps/latte_c/src/iterator/iterator.h"
#include "../../deps/latte_c/src/error/error.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* ========== LDB 序列化（复用 save 逻辑） ========== */
#define LDB_VERSION_STR "0.0.1"   /* LDB 文件格式版本号，与 save.c 保持一致 */

/* ========== 工具函数：向连接同步写入数据 ========== */

/**
 * 输入: conn - 连接, buf - 数据缓冲区, len - 数据长度
 * 输出/返回: 实际写入字节数，-1 表示失败
 * 功能: 对连接进行同步阻塞写入（使用 conn->type->sync_write）
 */
static ssize_t repl_sync_write(connection* conn, const char* buf, size_t len) {
    return conn->type->sync_write(conn, (char*)buf, (ssize_t)len, REPL_SYNCIO_TIMEOUT);
}

/**
 * 输入: conn - 连接, buf - 接收缓冲区, len - 最大读取长度
 * 输出/返回: 实际读取字节数，-1 表示失败
 * 功能: 从连接同步读取一行（以 \r\n 结尾）
 */
static ssize_t repl_sync_readline(connection* conn, char* buf, size_t len) {
    return conn->type->sync_readline(conn, buf, (ssize_t)len, REPL_SYNCIO_TIMEOUT);
}

/* ========== slave 侧：全量数据加载 ========== */

/**
 * 输入: server - 服务器实例
 * 输出/返回: 0 成功，-1 失败
 * 功能: 将 repl_transfer_buf 中的 LDB 数据加载到本地数据库
 *       复用 load_command 的核心逻辑：创建 buffer oio → 读取版本 → 读取注册表 →
 *       清空当前 DB → 逐条恢复键值对
 */
int replication_load_transfer_buf(redis_server_t* server) {
    LATTE_LIB_LOG(LOG_INFO, "replication: loading full-sync data, buf_len=%zu",
                  sds_len(server->repl_transfer_buf));

    /* 创建基于内存缓冲区的 oio，避免写临时文件 */
    oio* o = odb_oio_create_buffer();
    if (!o) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to create buffer oio");
        return -1;
    }

    /* 将接收到的数据写入 oio 缓冲区 */
    size_t data_len = sds_len(server->repl_transfer_buf);
    if (o->write(o, server->repl_transfer_buf, data_len) != data_len) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to write transfer buf to oio");
        odb_oio_free(o);
        return -1;
    }
    /* 重置读取位置，从头开始读 */
    odb_oio_buffer_rewind(o);

    /* 读取并校验 LDB 版本字符串 */
    sds file_version = odb_read_string(o);
    if (!file_version) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to read LDB version from transfer buf");
        odb_oio_free(o);
        return -1;
    }
    LATTE_LIB_LOG(LOG_INFO, "replication: LDB version in transfer buf: %s", (const char*)file_version);
    sds_delete(file_version);

    /* 加载对象类型注册表，得到 file type_id → current type_id 映射 */
    uint8_t id_map[256];
    latte_error_t* err = object_manager_load_registry(o, id_map);
    if (err) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to load object registry");
        error_delete(err);
        odb_oio_free(o);
        return -1;
    }

    /* 清空当前所有数据库 */
    db_clear(server);
    int current_db = 0;
    long long total_keys = 0;

    /* 逐条读取键值对，格式与 save/load 命令保持一致 */
    while (1) {
        uint8_t opcode;
        if (odb_read_u8(o, &opcode) == 0) break;

        if (opcode == 0xFD) {
            /* EOF 标记，加载完成 */
            LATTE_LIB_LOG(LOG_DEBUG, "replication: load hit EOF marker");
            break;
        }

        /* SELECTDB：0xFF + u32 dbid + u64 key_count */
        if (opcode == 0xFF) {
            uint32_t dbid;
            uint64_t db_key_count;
            if (odb_read_u32(o, &dbid) == 0) { odb_oio_free(o); return -1; }
            if (dbid >= (uint32_t)server->db_num) { odb_oio_free(o); return -1; }
            current_db = (int)dbid;
            if (odb_read_u64(o, &db_key_count) == 0) { odb_oio_free(o); return -1; }
            LATTE_LIB_LOG(LOG_DEBUG, "replication: SELECTDB dbid=%u key_count=%llu",
                          dbid, (unsigned long long)db_key_count);
            continue;
        }

        /* EXPIRETIME_MS：0xFE + u64 expire_ms，后跟 key */
        long long expire = 0;
        sds key = NULL;
        if (opcode == 0xFE) {
            uint64_t expire64;
            if (odb_read_u64(o, &expire64) == 0) { odb_oio_free(o); return -1; }
            expire = (long long)expire64;
            key = odb_read_string(o);
            if (key && sds_len(key) > 0) ((char*)key)[sds_len(key)] = '\0';
        } else {
            /* opcode 就是 key 长度的第一个字节（4 字节小端长度） */
            unsigned char len_buf[4];
            len_buf[0] = (unsigned char)opcode;
            if (o->read(o, len_buf + 1, 3) != 3) { odb_oio_free(o); return -1; }
            uint32_t key_len = (uint32_t)len_buf[0] | ((uint32_t)len_buf[1] << 8) |
                               ((uint32_t)len_buf[2] << 16) | ((uint32_t)len_buf[3] << 24);
            key = sds_new_len(SDS_NOINIT, key_len);
            if (!key || (key_len > 0 && o->read(o, key, key_len) != key_len)) {
                if (key) sds_delete(key);
                odb_oio_free(o);
                return -1;
            }
            if (key_len > 0) ((char*)key)[key_len] = '\0';
        }

        if (!key) { odb_oio_free(o); return -1; }

        /* 加载 value 对象 */
        void* obj_ptr = NULL;
        err = object_manager_load(o, &obj_ptr, id_map);
        if (err || !obj_ptr) {
            if (err) error_delete(err);
            sds_delete(key);
            odb_oio_free(o);
            return -1;
        }

        latte_object_t* val = (latte_object_t*)obj_ptr;
        redis_db_t* db = server->dbs + current_db;

        /* 将 key-value 写入当前数据库 */
        latte_object_t* key_obj = latte_object_string_new(key);
        if (!key_obj) {
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            return -1;
        }

        int ret = db_add_key_value(server, db, key_obj, val);
        latte_object_decr_ref_count(key_obj);
        if (ret != 0) {
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            return -1;
        }

        /* 如果有过期时间，设置 expire */
        if (expire > 0) {
            int didx = get_kv_store_index_for_key(key);
            dict_entry_t* de = kv_store_dict_find(db->keys, didx, key);
            if (de) {
                sds stored_key = (sds)dict_get_entry_key(de);
                if (stored_key) db_set_expire(server, db, stored_key, expire);
            }
        }

        total_keys++;
    }

    odb_oio_free(o);
    LATTE_LIB_LOG(LOG_INFO, "replication: full-sync load complete, total_keys=%lld", total_keys);
    return 0;
}

/* ========== slave 侧：连接/读事件处理 ========== */

/**
 * 输入: conn - 与 master 建立的连接
 * 输出/返回: 无
 * 功能: slave 侧连接建立回调。连接成功后切换为阻塞模式，发送 PING → 等待 PONG →
 *       发送 SYNC → 读取全量数据长度 → 切回非阻塞 → 注册异步读事件持续接收数据
 */
void replication_connect_handler(connection* conn) {
    redis_server_t* server = (redis_server_t*)connGetPrivateData(conn);
    if (!server) return;

    /* 检查连接是否成功 */
    if (conn_get_state(conn) != CONN_STATE_CONNECTED) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: connect to master failed: %s",
                      conn_get_last_error(conn));
        replication_stop(server);
        return;
    }

    LATTE_LIB_LOG(LOG_INFO, "replication: connected to master %s:%d",
                  server->master_host, server->master_port);

    /* 切换为阻塞模式，进行握手 */
    connBlock(conn);

    /* ---- 步骤1: 发送 PING ---- */
    char buf[256];
    if (repl_sync_write(conn, REPL_PROTO_PING_CMD, strlen(REPL_PROTO_PING_CMD)) < 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to send PING");
        replication_stop(server);
        return;
    }

    /* ---- 步骤2: 等待 PONG ---- */
    if (repl_sync_readline(conn, buf, sizeof(buf)) < 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to read PONG");
        replication_stop(server);
        return;
    }
    if (strncmp(buf, "+PONG", 5) != 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: unexpected reply to PING: %s", buf);
        replication_stop(server);
        return;
    }
    LATTE_LIB_LOG(LOG_INFO, "replication: got PONG from master");

    /* ---- 步骤3: 发送 SYNC（请求全量同步） ---- */
    if (repl_sync_write(conn, REPL_PROTO_SYNC_CMD, strlen(REPL_PROTO_SYNC_CMD)) < 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to send SYNC");
        replication_stop(server);
        return;
    }

    /* ---- 步骤4: 读取 bulk 长度 "$<len>\r\n" ---- */
    if (repl_sync_readline(conn, buf, sizeof(buf)) < 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to read bulk length");
        replication_stop(server);
        return;
    }
    if (buf[0] != '$') {
        LATTE_LIB_LOG(LOG_ERROR, "replication: unexpected bulk header: %s", buf);
        replication_stop(server);
        return;
    }
    long long bulk_len = atoll(buf + 1);
    if (bulk_len <= 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: invalid bulk length: %lld", bulk_len);
        replication_stop(server);
        return;
    }
    LATTE_LIB_LOG(LOG_INFO, "replication: full-sync data length = %lld bytes", bulk_len);

    /* ---- 步骤5: 读取全量 LDB 数据 ---- */
    /* 初始化或重置接收缓冲区 */
    if (server->repl_transfer_buf) {
        sds_delete(server->repl_transfer_buf);
    }
    server->repl_transfer_buf = sds_new_len(SDS_NOINIT, (size_t)bulk_len);
    if (!server->repl_transfer_buf) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: OOM allocating transfer buf");
        replication_stop(server);
        return;
    }

    /* 分块同步读取全量数据 */
    long long received = 0;
    char* dst = server->repl_transfer_buf;
    while (received < bulk_len) {
        ssize_t to_read = bulk_len - received;
        if (to_read > REPL_READ_BUF_SIZE) to_read = REPL_READ_BUF_SIZE;
        ssize_t n = conn->type->sync_read(conn, dst + received, to_read, REPL_SYNCIO_TIMEOUT);
        if (n <= 0) {
            LATTE_LIB_LOG(LOG_ERROR, "replication: sync_read failed at offset %lld", received);
            replication_stop(server);
            return;
        }
        received += n;
    }
    /* 读取结尾 \r\n */
    char crlf[2];
    conn->type->sync_read(conn, crlf, 2, REPL_SYNCIO_TIMEOUT);

    LATTE_LIB_LOG(LOG_INFO, "replication: full-sync transfer complete, %lld bytes received", received);

    /* ---- 步骤6: 加载全量数据到本地 DB ---- */
    server->repl_state = REPL_STATE_TRANSFER;
    if (replication_load_transfer_buf(server) != 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to load transfer data");
        replication_stop(server);
        return;
    }

    /* ---- 步骤7: 切为非阻塞，注册异步读事件接收增量命令 ---- */
    connNonBlock(conn);
    server->repl_state = REPL_STATE_CONNECTED;
    LATTE_LIB_LOG(LOG_INFO, "replication: full-sync done, entering incremental replication");

    /* 注册异步读处理：接收 master 推送的增量命令 */
    if (conn_set_read_handler(server->server.el, conn, replication_read_handler) != 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to set read handler for incremental repl");
        replication_stop(server);
        return;
    }
}

/**
 * 输入: conn - 与 master 建立的连接
 * 输出/返回: 无
 * 功能: slave 侧增量复制读事件处理器。从 master 接收增量命令（RESP inline 格式），
 *       在 server 的命令管理器中查找并执行，从而保持与 master 的数据一致性。
 *       每条命令格式: "<cmd> [arg1] [arg2] ...\r\n"
 */
void replication_read_handler(connection* conn) {
    redis_server_t* server = (redis_server_t*)connGetPrivateData(conn);
    if (!server || server->repl_state != REPL_STATE_CONNECTED) return;

    char buf[REPL_READ_BUF_SIZE];
    int nread = conn_read(conn, buf, sizeof(buf) - 1);
    if (nread <= 0) {
        if (nread == 0 || conn_get_state(conn) != CONN_STATE_CONNECTED) {
            LATTE_LIB_LOG(LOG_WARN, "replication: connection to master lost");
            replication_stop(server);
        }
        return;
    }
    buf[nread] = '\0';

    /* 逐行解析并执行增量命令（每条命令以 \r\n 结尾） */
    char* line = buf;
    while (line < buf + nread) {
        char* end = strstr(line, "\r\n");
        if (!end) break;
        *end = '\0';
        size_t line_len = end - line;
        if (line_len == 0) { line = end + 2; continue; }

        LATTE_LIB_LOG(LOG_DEBUG, "replication: got incremental cmd: %s", line);

        /* 解析命令行：按空格分割为 argv */
        int argc = 0;
        sds* argv = sds_split_len(line, (ssize_t)line_len, " ", 1, &argc);
        if (!argv || argc == 0) { line = end + 2; continue; }

        /* 查找命令 */
        redis_command_t* cmd = command_manager_lookup(server->command_manager, argv[0]);
        if (!cmd) {
            LATTE_LIB_LOG(LOG_WARN, "replication: unknown command from master: %s", argv[0]);
            sds_free_splitres(argv, argc);
            line = end + 2;
            continue;
        }

        /* 构造一个临时的 redis_client_t 来执行命令（复制上下文） */
        redis_client_t fake_client;
        memset(&fake_client, 0, sizeof(fake_client));
        fake_client.client.server = (latte_server_t*)server;
        fake_client.dbid = 0;
        fake_client.flag = CLIENT_MASTER;  /* 标记为来自 master 的命令，不再写回 backlog */
        fake_client.cmd = cmd;
        fake_client.argc = argc;

        /* 将 sds argv 包装为 latte_object_t* */
        latte_object_t** obj_argv = (latte_object_t**)zmalloc(sizeof(latte_object_t*) * argc);
        if (!obj_argv) {
            sds_free_splitres(argv, argc);
            line = end + 2;
            continue;
        }
        int i;
        for (i = 0; i < argc; i++) {
            obj_argv[i] = latte_object_string_new(sds_dup(argv[i]));
        }
        fake_client.argv = obj_argv;

        /* 执行命令（不写 backlog，避免无限循环） */
        cmd->proc(&fake_client);

        /* 释放临时 argv 对象 */
        for (i = 0; i < argc; i++) {
            if (obj_argv[i]) latte_object_decr_ref_count(obj_argv[i]);
        }
        zfree(obj_argv);
        sds_free_splitres(argv, argc);

        /* 更新 slave 的读取偏移量 */
        server->repl_read_offset += (long long)(line_len + 2);

        line = end + 2;
    }
}

/* ========== slave 侧：开始/停止复制 ========== */

/**
 * 输入: server - 服务器实例, host - master 主机地址, port - master 端口
 * 输出/返回: 0 成功，-1 失败
 * 功能: 开始连接到 master。创建 TCP 连接，注册连接完成回调 replication_connect_handler。
 *       若当前已有复制连接，先断开旧连接。
 */
int replication_start_connect(redis_server_t* server, const char* host, int port) {
    /* 先停止已有的复制连接 */
    if (server->repl_conn) {
        replication_stop(server);
    }

    /* 保存 master 地址 */
    if (server->master_host) sds_delete(server->master_host);
    server->master_host = sds_new(host);
    server->master_port = port;
    server->repl_state = REPL_STATE_CONNECTING;

    /* 创建新的 socket 连接 */
    connection* conn = connCreateSocket();
    if (!conn) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to create socket connection");
        server->repl_state = REPL_STATE_CONNECT;
        return -1;
    }

    /* 将 server 绑定到 conn 私有数据，供回调函数使用 */
    connSetPrivateData(conn, server);
    server->repl_conn = conn;

    LATTE_LIB_LOG(LOG_INFO, "replication: connecting to master %s:%d", host, port);

    /* 发起非阻塞 TCP 连接，连接成功后调用 replication_connect_handler */
    if (conn->type->connect(server->server.el, conn, host, port, NULL,
                            replication_connect_handler) != 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: connect failed: %s",
                      conn_get_last_error(conn));
        connClose(server->server.el, conn);
        server->repl_conn = NULL;
        server->repl_state = REPL_STATE_CONNECT;
        return -1;
    }

    LATTE_LIB_LOG(LOG_INFO, "replication: async connect initiated to %s:%d", host, port);
    return 0;
}

/**
 * 输入: server - 服务器实例
 * 输出/返回: 无
 * 功能: 停止复制。关闭与 master 的连接，释放接收缓冲区，重置状态为 REPL_STATE_NONE。
 */
void replication_stop(redis_server_t* server) {
    LATTE_LIB_LOG(LOG_INFO, "replication: stopping replication");

    if (server->repl_conn) {
        connClose(server->server.el, server->repl_conn);
        server->repl_conn = NULL;
    }

    if (server->repl_transfer_buf) {
        sds_delete(server->repl_transfer_buf);
        server->repl_transfer_buf = NULL;
    }

    server->repl_state = REPL_STATE_NONE;
    server->repl_read_offset = 0;
}

/* ========== 初始化 ========== */

/**
 * 输入: server - 服务器实例
 * 输出/返回: 无
 * 功能: 初始化复制相关字段，在 init_redis_server 中调用
 */
void replication_init(redis_server_t* server) {
    server->master_host = NULL;
    server->master_port = 0;
    server->repl_state = REPL_STATE_NONE;
    server->repl_conn = NULL;
    server->repl_transfer_buf = NULL;
    server->repl_master_initial_offset = -1;
    server->repl_read_offset = 0;
    server->slaves = list_new();
    server->repl_offset = 0;
}

/* ========== master 侧：序列化全量数据 ========== */

/**
 * 输入: server - 服务器实例, o - 输出 oio（缓冲区后端）
 * 输出/返回: 0 成功，-1 失败
 * 功能: 将当前所有数据库序列化为 LDB 格式写入 oio 缓冲区，
 *       流程与 save_command 完全一致（版本头 + 注册表 + SELECTDB + key/value + EOF）
 */
static int replication_serialize_all_dbs(redis_server_t* server, oio* o) {
    /* 写入 LDB 版本字符串 */
    if (odb_write_string(o, LDB_VERSION_STR, strlen(LDB_VERSION_STR)) == 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to write LDB version");
        return -1;
    }

    /* 写入对象类型注册表 */
    latte_error_t* err = object_manager_save_registry(o);
    if (err) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to save object registry");
        error_delete(err);
        return -1;
    }

    /* 遍历所有数据库，序列化键值对 */
    for (int dbid = 0; dbid < server->db_num; dbid++) {
        redis_db_t* db = server->dbs + dbid;
        if (!db || !db->keys) continue;

        /* 检查该库是否有键 */
        int has_keys = 0;
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (d && dict_size(d) > 0) { has_keys = 1; break; }
        }
        if (!has_keys) continue;

        /* 写入 SELECTDB：0xFF + dbid (u32) + key_count (u64) */
        if (odb_write_u8(o, 0xFF) == 0 || odb_write_u32(o, (uint32_t)dbid) == 0) return -1;
        odb_write_u64(o, db->keys->key_count);

        /* 遍历各 dict 分片，逐条写入 key/value */
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) continue;

            latte_iterator_t* it = dict_get_latte_iterator(d);
            if (!it) continue;

            while (latte_iterator_has_next(it)) {
                latte_pair_t* pair = (latte_pair_t*)latte_iterator_next(it);
                if (!pair) break;

                sds key = (sds)latte_pair_key(pair);
                latte_object_t* val = (latte_object_t*)latte_pair_value(pair);
                if (!key || !val) continue;

                /* 可选写入过期时间 */
                long long expire = db_get_expire(db, key);
                if (expire > 0) {
                    if (odb_write_u8(o, 0xFE) == 0 || odb_write_u64(o, (uint64_t)expire) == 0) {
                        latte_iterator_delete(it);
                        return -1;
                    }
                }

                /* 写入 key */
                if (odb_write_string(o, key, sds_len(key)) == 0) {
                    latte_iterator_delete(it);
                    return -1;
                }

                /* 写入 value */
                err = object_manager_save(o, val);
                if (err) {
                    error_delete(err);
                    latte_iterator_delete(it);
                    return -1;
                }
            }
            latte_iterator_delete(it);
        }
    }

    /* 写入 EOF 标记 */
    if (odb_write_u8(o, 0xFD) == 0) return -1;

    return 0;
}

/* ========== master 侧：全量同步给 slave ========== */

/**
 * 输入: server - 服务器实例, slave_client - 请求 SYNC 的 slave 客户端
 * 输出/返回: 无
 * 功能: master 侧全量同步处理：
 *       1. 序列化全量数据到内存 oio 缓冲区
 *       2. 以 "$<len>\r\n<data>\r\n" bulk string 格式发送给 slave
 *       3. 将 slave 客户端标记为 CLIENT_SLAVE，添加到 server->slaves 列表
 *       4. 将 slave 客户端的 read_reploff 设置为当前 repl_offset
 */
void replication_full_sync_to_slave(redis_server_t* server, redis_client_t* slave_client) {
    LATTE_LIB_LOG(LOG_INFO, "replication: starting full-sync to slave");

    /* 创建内存 oio 缓冲区，序列化全量数据 */
    oio* o = odb_oio_create_buffer();
    if (!o) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: failed to create oio for full-sync");
        add_reply_error(slave_client, "ERR full-sync failed: OOM");
        return;
    }

    if (replication_serialize_all_dbs(server, o) != 0) {
        LATTE_LIB_LOG(LOG_ERROR, "replication: serialize failed");
        odb_oio_free(o);
        add_reply_error(slave_client, "ERR full-sync failed: serialize error");
        return;
    }

    /* 获取序列化后的数据 */
    sds data_buf = o->io.buffer.ptr;
    size_t data_len = sds_len(data_buf);
    LATTE_LIB_LOG(LOG_INFO, "replication: full-sync data size = %zu bytes", data_len);

    /* 发送 bulk string 头："$<len>\r\n" */
    char header[64];
    int header_len = snprintf(header, sizeof(header), "$%zu\r\n", data_len);
    add_reply_proto((latte_client_t*)slave_client, header, header_len);

    /* 发送数据体 */
    add_reply_proto((latte_client_t*)slave_client, data_buf, data_len);

    /* 发送结尾 \r\n */
    add_reply_proto((latte_client_t*)slave_client, "\r\n", 2);

    odb_oio_free(o);

    /* 将该客户端标记为 slave，加入 slaves 列表，记录同步时的 offset */
    slave_client->flag |= CLIENT_SLAVE;
    slave_client->read_reploff = server->repl_offset;
    list_add_node_tail(server->slaves, slave_client);

    LATTE_LIB_LOG(LOG_INFO, "replication: full-sync sent to slave, offset=%lld",
                  server->repl_offset);
}

/* ========== master 侧：增量命令传播 ========== */

/**
 * 输入: server - 服务器实例
 * 输出/返回: 无
 * 功能: 定时调用（在 cron 中）。遍历所有 slave 客户端，从 backlog 中找出
 *       slave 尚未收到的增量命令并推送。命令以 inline 格式（"<cmd> args\r\n"）写入
 *       slave 的输出缓冲区。同时更新 slave 的 read_reploff 字段。
 *
 * 注意：backlog 是一个滑动窗口，仅保留最新的 max_entries 条命令。
 *       若 slave 的 read_reploff 太旧导致 backlog 中已没有对应命令，
 *       需要重新触发全量同步（当前实现：记录警告日志并跳过）。
 */
void replication_propagate_to_slaves(redis_server_t* server) {
    if (!server->slaves || list_length(server->slaves) == 0) return;
    if (!server->backlog || backlog_count(server->backlog) == 0) return;

    /* 获取 backlog 当前条目数 */
    size_t backlog_cnt = backlog_count(server->backlog);
    /* 遍历每个 slave */
    list_node_t* node = list_first(server->slaves);
    while (node) {
        redis_client_t* slave = (redis_client_t*)list_node_value(node);
        node = list_next(node);

        if (!slave || !(slave->flag & CLIENT_SLAVE)) continue;

        /* 计算从 backlog 中第几条开始推（slave 已收到 read_reploff 条） */
        long long slave_offset = slave->read_reploff;
        long long master_offset = server->repl_offset;

        if (slave_offset >= master_offset) continue; /* slave 已是最新 */

        /* 将 backlog 中所有新命令推送给 slave */
        /* 由于 backlog 是 list，只能从头遍历，通过计数跳过已发送的条目 */
        long long skip_count = (long long)backlog_cnt - (master_offset - slave_offset);
        if (skip_count < 0) {
            /* slave 落后太多，backlog 已无法覆盖，需要重新全量同步 */
            LATTE_LIB_LOG(LOG_WARN,
                "replication: slave too far behind (slave_offset=%lld master_offset=%lld), "
                "need full resync",
                slave_offset, master_offset);
            /* TODO: 触发重新全量同步 */
            continue;
        }

        /* 从 skip_count 处开始遍历 backlog，逐条推送给 slave */
        list_node_t* bl_node = list_first(server->backlog->entries);
        long long idx = 0;
        while (bl_node) {
            if (idx >= skip_count) {
                sds cmd_entry = (sds)list_node_value(bl_node);
                if (cmd_entry) {
                    /* 以 inline 格式写入 slave 输出缓冲区："<cmd entry>\r\n" */
                    add_reply_proto((latte_client_t*)slave,
                                    cmd_entry, sds_len(cmd_entry));
                    add_reply_proto((latte_client_t*)slave, "\r\n", 2);
                    slave->read_reploff++;
                }
            }
            idx++;
            bl_node = list_next(bl_node);
        }
    }
}
