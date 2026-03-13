/**
 * slaveof.c - SLAVEOF 命令实现
 *
 * 命令格式：
 *   SLAVEOF host port   - 使当前节点成为指定 master 的 slave，触发全量+增量同步
 *   SLAVEOF NO ONE      - 停止复制，当前节点恢复为独立的 master
 *
 * 同时处理 SYNC 命令（slave 在握手阶段发送）：
 *   SYNC                - master 侧：对发出此命令的客户端执行全量数据发送
 */
#include "command_manager.h"
#include "../redis/server.h"
#include "../redis/client.h"
#include "../redis/replication.h"
#include "../shared/shared.h"
#include "debug/latte_debug.h"
#include "sds/sds.h"
#include "../object/string.h"
#include <string.h>
#include <stdlib.h>

/**
 * SLAVEOF host port | SLAVEOF NO ONE
 *
 * 输入: c - Redis 客户端上下文
 * 输出/返回: 无（通过 add_reply 返回结果）
 * 功能:
 *   - "SLAVEOF NO ONE": 停止复制，当前节点变为独立 master
 *   - "SLAVEOF host port": 设置当前节点为指定 master 的 slave，
 *                          异步发起连接，完成 PING → SYNC → 全量 → 增量同步流程
 */
void slaveof_command(redis_client_t* c) {
    redis_server_t* server = (redis_server_t*)c->client.server;

    /* 参数校验：必须是 3 个参数 */
    if (c->argc != 3) {
        add_reply_error(c, "ERR wrong number of arguments for 'slaveof' command");
        return;
    }

    /* 获取参数字符串 */
    const char* arg1 = (const char*)c->argv[1]->ptr;
    const char* arg2 = (const char*)c->argv[2]->ptr;

    /* ---- SLAVEOF NO ONE：停止复制 ---- */
    if (strcasecmp(arg1, "NO") == 0 && strcasecmp(arg2, "ONE") == 0) {
        if (server->repl_state == REPL_STATE_NONE) {
            /* 已经是独立节点，无需操作 */
            LATTE_LIB_LOG(LOG_INFO, "slaveof: already standalone master");
        } else {
            LATTE_LIB_LOG(LOG_INFO, "slaveof: NO ONE - stopping replication");
            replication_stop(server);
            /* 清除 master_host/port 配置 */
            if (server->master_host) {
                sds_delete(server->master_host);
                server->master_host = NULL;
            }
            server->master_port = 0;
        }
        add_reply(c, shared.ok);
        return;
    }

    /* ---- SLAVEOF host port：开始复制 ---- */
    /* 解析 port 参数 */
    long port = atol(arg2);
    if (port <= 0 || port > 65535) {
        add_reply_error(c, "ERR Invalid master port");
        return;
    }

    /* 如果当前已经在复制同一个 master，直接返回 OK（幂等） */
    if (server->master_host &&
        strcasecmp(server->master_host, arg1) == 0 &&
        server->master_port == (int)port &&
        server->repl_state == REPL_STATE_CONNECTED) {
        LATTE_LIB_LOG(LOG_INFO, "slaveof: already connected to %s:%ld", arg1, port);
        add_reply(c, shared.ok);
        return;
    }

    LATTE_LIB_LOG(LOG_INFO, "slaveof: becoming slave of %s:%ld", arg1, port);

    /* 异步发起连接，完成握手和全量同步 */
    if (replication_start_connect(server, arg1, (int)port) != 0) {
        add_reply_error_format(c, "ERR Failed to connect to master %s:%ld", arg1, port);
        return;
    }

    add_reply(c, shared.ok);
}

/**
 * SYNC 命令（slave 在握手阶段发送，master 收到后执行全量同步）
 *
 * 输入: c - Redis 客户端上下文（此时为 slave 发来的请求）
 * 输出/返回: 无（通过 add_reply 发送全量数据）
 * 功能: master 侧处理 SYNC 命令：
 *   1. 将客户端标记为 slave (CLIENT_SLAVE)
 *   2. 调用 replication_full_sync_to_slave 序列化并发送全量数据
 *   3. slave 接收完毕后自动进入增量复制模式
 */
void sync_command(redis_client_t* c) {
    redis_server_t* server = (redis_server_t*)c->client.server;
    LATTE_LIB_LOG(LOG_INFO, "sync: received SYNC from client");
    /* 执行全量同步：序列化所有 DB 数据并发送给 slave */
    replication_full_sync_to_slave(server, c);
}
