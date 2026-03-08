/*
 * SAVE / LOAD 命令实现：将当前数据库持久化到 ldb 文件，或从 ldb 文件恢复。
 *
 * LDB 文件格式（与 odb/object_manager 配合）：
 *   0. 版本字符串（odb 格式：4 字节长度 + 内容，LOAD 时校验兼容性）
 *   1. 对象类型注册表（object_manager_save_registry / load_registry）
 *   2. 按数据库顺序写入：
 *      - 0xFF + u32 dbid  (SELECTDB，仅当该库有 key 时写入)
 *      - 对每个 key：
 *        - 可选：0xFE + u64 expire_ms (EXPIRETIME_MS)
 *        - key 字符串（odb_write_string / odb_read_string）
 *        - value 对象（object_manager_save / load）
 *   3. 0xFD (EOF)
 */
#define LDB_VERSION_STR "0.0.1"
#include "command_manager.h"
#include "../shared/shared.h"
#include "../redis/db.h"
#include "../redis/server.h"
#include "../object/string.h"
#include "debug/latte_debug.h"
#include "../../deps/latte_c/src/odb/odb.h"
#include "../../deps/latte_c/src/object/object_manager.h"
#include "../../deps/latte_c/src/dict/dict.h"
#include "../../deps/latte_c/src/iterator/iterator.h"
#include "../../deps/latte_c/src/error/error.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

/*
 * ldb_version_compare - 比较语义化版本字符串 "X.Y.Z"
 * 返回: <0 表示 a < b，0 表示 a == b，>0 表示 a > b；解析失败返回 INT_MIN
 */
static int ldb_version_compare(const char* a, const char* b) {
    int ma = 0, mi = 0, pa = 0;
    int mb = 0, mib = 0, pb = 0;
    if (!a || !b) return INT_MIN;
    if (sscanf(a, "%d.%d.%d", &ma, &mi, &pa) != 3) return INT_MIN;
    if (sscanf(b, "%d.%d.%d", &mb, &mib, &pb) != 3) return INT_MIN;
    if (ma != mb) return ma < mb ? -1 : 1;
    if (mi != mib) return mi < mib ? -1 : 1;
    if (pa != pb) return pa < pb ? -1 : 1;
    return 0;
}

/*
 * save_command - 将当前服务器所有数据库持久化到 ldb 文件
 *
 * 用法：SAVE [filename]
 *   - 无参：使用 config 中的 ldb_file，若未配置则使用 "dump.ldb"
 *   - filename：指定保存路径（覆盖默认）
 *
 * 流程：
 *   1. 解析目标文件路径（config 或 argv[1]）
 *   2. 打开文件，创建 oio 封装
 *   3. 写入对象类型注册表（供 load 时还原 type_id 映射）
 *   4. 按 db 顺序遍历：对有 key 的 db 写 SELECTDB，再遍历该 db 下所有 dict 的 key，
 *      对每个 key 可选写过期时间、key 字符串、value 对象
 *   5. 写 EOF 标记并 flush/关闭
 * 返回：OK 或 ERR
 */
void save_command(redis_client_t* c) {
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: start");
    redis_server_t* server = (redis_server_t*)c->client.server;

    /* 确定输出文件路径：优先 config->ldb_file，否则默认 "dump.ldb" */
    const char* filename = "dump.ldb";
    if (server->config && server->config->ldb_file && sds_len(server->config->ldb_file) > 0)
        filename = server->config->ldb_file;

    /* 参数校验：SAVE 无参或 SAVE filename，禁止多余参数 */
    if (c->argc > 2) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }
    if (c->argc == 2) {
        /* SAVE filename：argv[1] 必须是字符串对象 */
        if (!sds_encoded_object(c->argv[1])) {
            add_reply_error(c, "ERR Invalid filename");
            return;
        }
        filename = (const char*)c->argv[1]->ptr;
    }

    /* 以二进制写方式打开文件 */
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: opening file %s", filename);
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to open file %s", filename);
        add_reply_error_format(c, "ERR Failed to open file: %s", filename);
        return;
    }
    
    /* 使用 odb 的 oio 封装 FILE*，统一读写 u8/u32/u64/string 等 */
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: creating oio");
    oio* o = odb_oio_create_file(fp);
    if (!o) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to create oio");
        fclose(fp);
        add_reply_error(c, "ERR Failed to create oio");
        return;
    }

    /* 写入 LDB 版本字符串（odb 格式：长度 + 内容），LOAD 时用于校验 */
    if (odb_write_string(o, LDB_VERSION_STR, strlen(LDB_VERSION_STR)) == 0) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write LDB version");
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to write LDB version");
        return;
    }

    /* 先写入对象类型注册表：type_name -> type_id 映射，load 时用 id_map 还原 */
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: saving registry");
    latte_error_t* err = object_manager_save_registry(o);
    if (err) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to save registry");
        error_delete(err);
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to save object registry");
        return;
    }
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: registry saved");
    
    long long total_keys = 0;

    /* 遍历所有逻辑库（db 0 .. db_num-1），只对有 key 的库写 SELECTDB + key/value */
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: iterating databases, db_num=%d", server->db_num);
    if (!server->dbs) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: server->dbs is NULL");
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Internal error: server->dbs is NULL");
        return;
    }
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: server->dbs is valid, starting loop");
    /* Iterate through all databases */
    for (int dbid = 0; dbid < server->db_num; dbid++) {
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: checking db %d", dbid);
        redis_db_t* db = server->dbs + dbid;
        if (!db) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d is NULL", dbid);
            continue;
        }
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d is valid, checking keys", dbid);
        if (!db->keys) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d keys is NULL", dbid);
            continue;
        }
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d has keys, num_dicts=%lld", dbid, (long long)db->keys->num_dicts);
        /* 判断当前 db 是否至少有一个 dict 非空，避免写入空库的 SELECTDB */
        int has_keys = 0;
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d checking dict %lld", dbid, didx);
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) {
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is NULL", dbid, didx);
                continue;
            }
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is valid, getting size", dbid, didx);
            long long dict_sz = dict_size(d);
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld size=%lld", dbid, didx, dict_sz);
                if (dict_sz > 0) {
                    LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld has %lld keys", dbid, didx, dict_sz);
                    has_keys = 1;
                    break;
                }
            }
            /* 仅在有 key 的 db 前写 SELECTDB */
            if (!has_keys) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d has no keys, skipping", dbid);
            continue;
        }
        
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: saving db %d", dbid);
        /* 写入 SELECTDB：0xFF + dbid (u32)，load 时读到 0xFF 会切换 current_db */
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: writing SELECTDB for db %d", dbid);
        if (odb_write_u8(o, 0xFF) == 0 || odb_write_u32(o, dbid) == 0) {
            LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write SELECTDB for db %d", dbid);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to save database id");
            return;
        }
        /* 添加db个数*/
        odb_write_u64(o, db->keys->key_count);

        LATTE_LIB_LOG(LOG_DEBUG, "save_command: iterating dicts in db %d", dbid);
        /* 遍历该 db 下 kv_store 中的每个 dict（分片），逐个 key 写入 */
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d getting dict %lld", dbid, didx);
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) {
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is NULL, skipping", dbid, didx);
                continue;
            }
            
            
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is valid, creating iterator", dbid, didx);
            /* 创建 dict 迭代器，遍历 (key, value) 对 */
            latte_iterator_t* it = dict_get_latte_iterator(d);
            if (!it) {
                LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to create iterator for db %d dict %lld", dbid, didx);
                continue;
            }
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld iterator created, iterating keys", dbid, didx);
            int key_count = 0;
            while (latte_iterator_has_next(it)) {
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld has next key %d", dbid, didx, key_count);
                latte_pair_t* pair = (latte_pair_t*)latte_iterator_next(it);
                if (!pair) {
                    LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld pair is NULL, breaking", dbid, didx);
                    break;
                }
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld got pair, getting key and value", dbid, didx);
                sds key = (sds)latte_pair_key(pair);

                latte_object_t* val = (latte_object_t*)latte_pair_value(pair);
                
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld key=%s val=%p", dbid, didx, key, val);
                if (!key || !val) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: db %d dict %lld key or val is NULL/invalid, skipping (key=%p (0x%lx), val=%p). This indicates a problem with dict entry structure!", dbid, didx, key, (unsigned long)key, val);
                    continue;
                }
                
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld processing key %d, key_len=%zu", dbid, didx, key_count++, sds_len(key));
                
                /* 若该 key 有过期时间则写入 0xFE + expire_ms (u64) */
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld getting expire for key", dbid, didx);
                long long expire = db_get_expire(db, key);
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld expire=%lld", dbid, didx, expire);
                
                if (expire > 0) {
                    LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld writing expire time", dbid, didx);
                    if (odb_write_u8(o, 0xFE) == 0 || odb_write_u64(o, (uint64_t)expire) == 0) {
                        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write expire time");
                        latte_iterator_delete(it);
                        odb_oio_free(o);
                        fclose(fp);
                        add_reply_error(c, "ERR Failed to save expiration time");
                        return;
                    }
                }
                
                /* 写入 key：长度 + 二进制内容（odb_write_string） */
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld writing key, len=%zu", dbid, didx, sds_len(key));
                if (odb_write_string(o, key, sds_len(key)) == 0) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write key");
                    latte_iterator_delete(it);
                    odb_oio_free(o);
                    fclose(fp);
                    add_reply_error(c, "ERR Failed to save key");
                    return;
                }
                
                /* 写入 value：通过 object_manager_save 按类型序列化（含 type_id + 类型相关数据） */
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld saving object type=%u, refcount=%d", 
                              dbid, didx, (unsigned)val->type, val->refcount);
                err = object_manager_save(o, val);
                if (err) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to save object, error code=%d", err->code);
                    error_delete(err);
                    protected_dict_iterator_delete(it);
                    odb_oio_free(o);
                    fclose(fp);
                    add_reply_error(c, "ERR Failed to save object");
                    return;
                }
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld object saved successfully", dbid, didx);
                
                total_keys++;
            }
            latte_iterator_delete(it);
        }
    }
    //TODO 增加crc检验
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: total keys saved=%lld", total_keys);
    /* 写入文件结束标记 0xFD，load 时读到即结束 */
    if (odb_write_u8(o, 0xFD) == 0) {
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to write EOF");
        return;
    }
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: writing EOF marker");
    /* 刷盘并释放 oio、关闭文件 */
    if (o->flush(o) == 0) {
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to flush file");
        return;
    }
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: freeing oio");
    odb_oio_free(o);
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: closing file");
    fclose(fp);
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: adding reply ok");
    add_reply(c, shared.ok);
}

/*
 * load_command - 从 ldb 文件恢复所有数据库到当前服务器
 *
 * 用法：LOAD [filename]
 *   - 无参：使用 config 中的 ldb_file，未配置则 "dump.ldb"
 *   - filename：指定要加载的文件路径
 *
 * 流程：
 *   1. 解析源文件路径，以二进制读打开并创建 oio
 *   2. 加载对象类型注册表，得到 id_map（文件中的 type_id -> 当前 type_id）
 *   3. 清空当前所有 db：两遍遍历（先收集 key 再删除），避免迭代中修改 dict
 *   4. 按 SAVE 的格式循环读：0xFF 切换 current_db，0xFE 读过期时间，再读 key、value，
 *      写入当前 db 并可选设置 expire，直到读到 0xFD EOF
 * 返回：OK 或 ERR
 */
void load_command(redis_client_t* c) {
    LATTE_LIB_LOG(LOG_INFO, "load_command: start");
    redis_server_t* server = (redis_server_t*)c->client.server;
    LATTE_LIB_LOG(LOG_INFO, "load_command: server=%p db_num=%d", (void*)server, server ? server->db_num : 0);

    /* 确定输入文件路径：优先 config->ldb_file，否则默认 "dump.ldb" */
    const char* filename = "dump.ldb";
    if (server->config && server->config->ldb_file && sds_len(server->config->ldb_file) > 0)
        filename = server->config->ldb_file;

    /* 参数校验：LOAD 无参或 LOAD filename */
    if (c->argc > 2) {
        LATTE_LIB_LOG(LOG_DEBUG, "load_command: wrong argc=%d", c->argc);
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }
    if (c->argc == 2) {
        /* LOAD filename：argv[1] 必须为字符串 */
        if (!sds_encoded_object(c->argv[1])) {
            add_reply_error(c, "ERR Invalid filename");
            return;
        }
        filename = (const char*)c->argv[1]->ptr;
    }

    /* 以二进制读方式打开 ldb 文件 */
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: opening file %s", filename);
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        LATTE_LIB_LOG(LOG_ERROR, "load_command: failed to open file %s", filename);
        add_reply_error_format(c, "ERR Failed to open file: %s", filename);
        return;
    }
    /* 使用 oio 封装 FILE*，统一按 u8/u32/u64/string 读取 */
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: file opened, creating oio");
    oio* o = odb_oio_create_file(fp);
    if (!o) {
        fclose(fp);
        LATTE_LIB_LOG(LOG_ERROR, "load_command: failed to create oio");
        add_reply_error(c, "ERR Failed to create oio");
        return;
    }

    /* 读取并校验 LDB 版本字符串（兼容低版本：仅拒绝比当前新的版本） */
    sds file_version = odb_read_string(o);
    if (!file_version) {
        LATTE_LIB_LOG(LOG_ERROR, "load_command: failed to read LDB version");
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to read LDB version");
        return;
    }
    int cmp = ldb_version_compare((const char*)file_version, LDB_VERSION_STR);
    if (cmp == INT_MIN) {
        LATTE_LIB_LOG(LOG_ERROR, "load_command: invalid LDB version string '%s'", (const char*)file_version);
        sds_delete(file_version);
        odb_oio_free(o);
        fclose(fp);
        add_reply_error_format(c, "ERR Invalid LDB version string '%s' (expected X.Y.Z)", (const char*)file_version);
        return;
    }
    if (cmp > 0) {
        LATTE_LIB_LOG(LOG_ERROR, "load_command: LDB file version '%s' is newer than current '%s'", (const char*)file_version, LDB_VERSION_STR);
        sds_delete(file_version);
        odb_oio_free(o);
        fclose(fp);
        add_reply_error_format(c, "ERR LDB file version '%s' is newer than current '%s', cannot load", (const char*)file_version, LDB_VERSION_STR);
        return;
    }
    sds_delete(file_version);
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: LDB version compatible (file <= %s)", LDB_VERSION_STR);

    /* 加载对象类型注册表，得到文件内 type_id -> 当前 type_id 的 id_map，供后续 object_manager_load 使用 */
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: loading object registry");
    uint8_t id_map[256];
    latte_error_t* err = object_manager_load_registry(o, id_map);
    if (err) {
        LATTE_LIB_LOG(LOG_ERROR, "load_command: failed to load registry");
        error_delete(err);
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to load object registry");
        return;
    }
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: registry loaded, clearing existing data, db_num=%d", server->db_num);

    db_clear(server);
    int current_db = 0;
    long long total_keys = 0;
    uint64_t db_key_count;
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: clear done, starting load loop");

    /* 按 SAVE 格式循环读：opcode 决定 SELECTDB / EXPIRETIME_MS / key+value / EOF */
    while (1) {
        uint8_t opcode;
        if (odb_read_u8(o, &opcode) == 0) {
            LATTE_LIB_LOG(LOG_DEBUG, "load_command: read u8 failed (EOF or error), breaking");
            break;
        }
        LATTE_LIB_LOG(LOG_DEBUG, "load_command: opcode=0x%02X", opcode);

        if (opcode == 0xFD) {
            LATTE_LIB_LOG(LOG_DEBUG, "load_command: EOF marker, breaking");
            break;
        }
        /* 0xFF：切换当前库，后续 key/value 写入 current_db；注意下一字节是 key 长度首字节，不是 opcode */
        if (opcode == 0xFF) {
            uint32_t dbid;
            if (odb_read_u32(o, &dbid) == 0) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read database id");
                return;
            }
            if (dbid >= server->db_num) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error_format(c, "ERR Invalid database id: %u", dbid);
                return;
            }
            current_db = (int)dbid;
            LATTE_LIB_LOG(LOG_DEBUG, "load_command: SELECTDB dbid=%u", dbid);

            if (odb_read_u64(o, &db_key_count) == 0) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read database key count");
                return;
            }
            LATTE_LIB_LOG(LOG_DEBUG, "load_command: dbid=%u key_count=%lld", dbid, db_key_count);       
            continue;
        }
        /* 0xFE：本条 key 有过期时间，读 u64 得到 expire_ms，然后紧跟 key（odb 格式：4 字节长度 + 数据） */
        long long expire = 0;
        if (opcode == 0xFE) {
            uint64_t expire64;
            if (odb_read_u64(o, &expire64) == 0) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read expiration time");
                return;
            }
            expire = (long long)expire64;
            LATTE_LIB_LOG(LOG_DEBUG, "load_command: EXPIRETIME_MS expire=%lld", expire);
        }
        /* 读 key：若刚读完 0xFE+expire，则下一字节是 key 的 4 字节长度首字节；否则当前 opcode 就是长度首字节（被误读为 opcode），需用其与后续 3 字节拼出长度再读 key 内容 */
        LATTE_LIB_LOG(LOG_DEBUG, "load_command: reading key");
        sds key;
        if (opcode == 0xFE) {
            key = odb_read_string(o);
            /* sds_new(key->ptr) 等依赖 C 字符串，需保证末尾有 \0 */
            if (key && sds_len(key) > 0) ((char*)key)[sds_len(key)] = '\0';
        } else {
            /* opcode 实为 key 长度的第一个字节，与 odb 的 4 字节小端长度一致 */
            unsigned char len_buf[4];
            len_buf[0] = (unsigned char)opcode;
            if (o->read(o, len_buf + 1, 3) != 3) {
                LATTE_LIB_LOG(LOG_ERROR, "load_command: failed to read key length (3 bytes)");
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read key");
                return;
            }
            uint32_t key_len = (uint32_t)len_buf[0] | ((uint32_t)len_buf[1] << 8) | ((uint32_t)len_buf[2] << 16) | ((uint32_t)len_buf[3] << 24);
            key = sds_new_len(SDS_NOINIT, key_len);
            if (!key || (key_len > 0 && o->read(o, key, key_len) != key_len)) {
                if (key) sds_delete(key);
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read key");
                return;
            }
            /* sds_new(key->ptr) 等依赖 C 字符串，需保证末尾有 \0 */
            if (key_len > 0) ((char*)key)[key_len] = '\0';
        }
        if (!key) {
            LATTE_LIB_LOG(LOG_ERROR, "load_command: failed to read key");
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to read key");
            return;
        }
        /* 用 object_manager_load 按 type_id + id_map 反序列化出 value 对象 */
        LATTE_LIB_LOG(LOG_DEBUG, "load_command: loading object for key \"%s\" len=%zu", key, sds_len(key));
        void* obj_ptr = NULL;
        err = object_manager_load(o, &obj_ptr, id_map);
        if (err) {
            LATTE_LIB_LOG(LOG_ERROR, "load_command: object_manager_load failed");
            error_delete(err);
            sds_delete(key);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to load object");
            return;
        }
        
        latte_object_t* val = (latte_object_t*)obj_ptr;
        if (!val) {
            sds_delete(key);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to load object");
            return;
        }
        /* 将 key-value 写入 current_db，并处理引用计数 */
        redis_db_t* db = server->dbs + current_db;
        if (!db) {
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Invalid database");
            return;
        }
        
        latte_object_t* key_obj = latte_object_string_new( key);
        if (!key_obj) {
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to create key object");
            return;
        }
        
        LATTE_LIB_LOG(LOG_DEBUG, "load_command: adding key to db %d", current_db);
        int ret = db_add_key_value(server, db, key_obj, val);
        latte_object_decr_ref_count(key_obj);
        
        if (ret != 0) {
            LATTE_LIB_LOG(LOG_ERROR, "load_command: db_add_key_value failed ret=%d", ret);
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to add key-value to database");
            return;
        }
        /* 若本条在文件中带有过期时间，则在 db 中查存储的 key 并设置 expire */
        if (expire > 0) {
            int didx = get_kv_store_index_for_key(key);
            dict_entry_t* de = kv_store_dict_find(db->keys, didx, key);
            if (de) {
                sds stored_key = (sds)dict_get_entry_key(de);
                if (stored_key) {
                    if (db_set_expire(server, db, stored_key, expire) != 0) {
                        /* 设置过期失败不中断整次 load，仅跳过该 key 的 expire */
                    }
                }
            }
        }
        /* key 已交给 key_obj（latte_object_string_new 取得所有权），勿再 sds_delete(key) */
        total_keys++;
    }
    
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: load loop done, total_keys=%lld", total_keys);
    odb_oio_free(o);
    fclose(fp);
    LATTE_LIB_LOG(LOG_DEBUG, "load_command: sending OK reply");
    add_reply(c, shared.ok);
}
