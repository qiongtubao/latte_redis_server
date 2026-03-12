#include "client.h"
#include "server.h"
#include "debug/latte_debug.h"
#include <string.h>
#include "../object/string.h"
#include "utils/utils.h"
/** 获取客户端的类别，用于对不同类别的客户端执行限制
 * 输入: c - Redis客户端指针
 * 返回: 客户端类型，可能的值：
 *       CLIENT_TYPE_NORMAL -> 普通客户端
 *       CLIENT_TYPE_SLAVE  -> 从节点
 *       CLIENT_TYPE_PUBSUB -> 订阅了发布/订阅频道的客户端
 *       CLIENT_TYPE_MASTER -> 代表复制主节点的客户端
 */
int get_client_type(redis_client_t *c) {
    if (c->flag & CLIENT_MASTER) return CLIENT_TYPE_MASTER;
    /* 尽管 MONITOR 客户端被标记为副本，但我们希望将它们作为普通客户端暴露 */
    if ((c->flag & CLIENT_SLAVE) && !(c->flag & CLIENT_MONITOR))
        return CLIENT_TYPE_SLAVE;
    if (c->flag & CLIENT_PUBSUB) return CLIENT_TYPE_PUBSUB;
    return CLIENT_TYPE_NORMAL;
}

/** 执行命令并将数据写入 backlog
 * 输入: rc - Redis客户端指针, flags - 调用标志
 * 功能: 1. 调用命令处理函数执行命令
 *       2. 将命令名和参数写入服务器 backlog（命令名 + 参数，空格分隔）
 */
void call(redis_client_t* rc, int flags) {
    rc->cmd->proc(rc);

    /* 命令执行后将数据写入 server backlog（命令名 + 参数，空格分隔） */
    redis_server_t* server = (redis_server_t*)rc->client.server;
    if (server && server->backlog && rc->cmd && rc->cmd->name) {
        sds entry = sds_empty();
        if (entry) {
            const char* name = rc->cmd->name;
            entry = sds_cat_len(entry, name, strlen(name)); // 添加命令名
            for (int j = 1; entry && j < rc->argc; j++) {    // 逐个添加参数
                entry = sds_cat_len(entry, " ", 1);          // 参数间用空格分隔
                if (entry && rc->argv[j] && rc->argv[j]->ptr) {
                    size_t len = string_object_len(rc->argv[j]);
                    if (len > 0)
                        entry = sds_cat_len(entry, rc->argv[j]->ptr, len);
                }
            }
            if (entry)
                backlog_add(server->backlog, entry);
            else
                sds_delete(entry);
        }
    }
}

/** 查找并执行命令
 * 输入: rc - Redis客户端指针
 * 返回: 0 表示成功，-1 表示失败
 * 功能: 1. 从 argv[0] 构建命令名称用于字典查找
 *       2. 在命令管理器中查找命令
 *       3. 调用 call 函数执行命令
 */
int process_command(redis_client_t* rc) {
    redis_server_t* server = (redis_server_t*)rc->client.server;
    // if (!server.lua_timedout) {
    //     /* Both EXEC and EVAL call call() directly so there should be
    //      * no way in_exec or in_eval or propagate_in_transaction is 1.
    //      * That is unless lua_timedout, in which case client may run
    //      * some commands. */
    //     serverAssert(!server.propagate_in_transaction);
    //     serverAssert(!server.in_exec);
    //     serverAssert(!server.in_eval);
    // }
    // moduleCallCommandFilters(c);
    /* 现在查找命令并尽快检查简单的错误条件，如错误的参数数量、错误的命令名等
     * argv[0] 可能是 EMBSTR（ptr = 内联数据，不是完整的 sds）；为字典查找构建一个合适的 sds */
    size_t cmdlen = string_object_len(rc->argv[0]);
    sds cmd_sds = sds_new_len(rc->argv[0]->ptr, cmdlen);
    rc->cmd = rc->lastcmd = command_manager_lookup(server->command_manager, cmd_sds);
    sds_delete(cmd_sds);
    if (rc->cmd == NULL) {
        LATTE_LIB_LOG(LOG_ERROR, "command not found: %s", rc->argv[0]->ptr);
        return -1;
    }
    if (rc->cmd->name && strcmp(rc->cmd->name, "load") == 0) {
        LATTE_LIB_LOG(LOG_INFO, "process_command: about to call load_command");
    }
    call(rc, CMD_CALL_FULL);  // 执行命令
    return 0;
}

/** 释放客户端命令参数数组
 * 输入: rc - Redis客户端指针
 * 功能: 释放所有参数对象的引用计数并重置相关字段
 */
static void free_client_argv(redis_client_t* rc) {
    int j;
    for (j = 0; j < rc->argc; j++)
        latte_object_decr_ref_count(rc->argv[j]);  // 减少每个参数对象的引用计数
    rc->argc = 0;           // 重置参数数量
    rc->cmd = NULL;         // 清空当前命令
    rc->argv_len_sum = 0;   // 重置参数长度总和
}

/** 重置客户端状态以供下次命令使用
 * 输入: rc - Redis客户端指针
 * 功能: 清理当前命令相关状态，准备处理下一个命令
 */
void reset_redis_client(redis_client_t* rc) {
    redis_command_proc_func*  prev_cmd = rc->cmd ?  rc->cmd->proc : NULL;
    free_client_argv(rc);       // 释放当前命令参数
    rc->req_type = 0;          // 重置请求类型
    rc->multi_bulk_len = 0;    // 重置多批量长度
    rc->bulk_len = -1;         // 重置批量长度为未知状态
    // if (rc->swap_ctx) {
        // swap_ctx_delete(rc->swap_ctx);
    // }

    // if (!(rc->flag & CLIENT_MULTI) && prev_cmd != askingCommand)
    //      c->flags &= ~CLIENT_ASKING;

    // if (!(rc->flag & CLIENT_MULTI) && prev_cmd != clientCommand)
    //      c->flags &= ~CLIENT_TRACKING_CACHING;

    /* 移除 CLIENT_REPLY_SKIP 标志以便下一个命令的回复能被发送，
     * 但如果刚处理的命令是 "CLIENT REPLY SKIP" 则设置该标志 */

    // c->flags &= ~CLIENT_REPLY_SKIP;
    // if (c->flags & CLIENT_REPLY_SKIP_NEXT) {
    //     c->flags |= CLIENT_REPLY_SKIP;
    //     c->flags &= ~CLIENT_REPLY_SKIP_NEXT;
    // }

}

/** 命令处理完成后的清理工作
 * 输入: rc - Redis客户端指针
 * 功能: 重置客户端状态，为下一个命令做准备
 */
void command_processed(redis_client_t* rc) {
    reset_redis_client(rc);
}

/** 保存/恢复 current_client 上下文并执行命令
 * 输入: rc - Redis客户端指针
 * 返回: 0 表示成功，-1 表示客户端已死亡
 * 功能: 1. 保存当前客户端上下文
 *       2. 设置当前客户端为 rc
 *       3. 执行命令并处理结果
 *       4. 恢复之前的客户端上下文
 */
int process_command_and_reset_client(redis_client_t* rc) {
    LATTE_LIB_LOG(LOG_INFO, "process_command_and_reset_client");
    int deadclient = 0;
    redis_server_t* server = (redis_server_t*)rc->client.server;
    redis_client_t *old_client = server->current_client;  // 保存旧的客户端
    server->current_client = rc;                          // 设置当前客户端
    if (process_command(rc) == 0) {
        command_processed(rc);
    }
    if (server->current_client == NULL) deadclient = 1;   // 检查客户端是否已死亡
    /*
     * 恢复旧客户端，这是必需的，因为当脚本超时时，我们会从 processEventsWhileBlocked 进入此代码。
     * 这将导致设置 server.current_client。如果不恢复，我们将向调用者返回 1，
     * 这将错误地表示客户端已死亡并停止从其缓冲区读取。
     */
    server->current_client = old_client;
    /* performEvictions 可能会刷新从节点输出缓冲区。这可能导致从节点（可能是活动客户端）被释放。 */
    return deadclient ? -1 : 0;
}

/** 设置协议错误处理
 * 输入: errstr - 错误字符串, rc - Redis客户端指针
 * 功能: 记录协议错误信息
 */
void set_protocol_error(const char* errstr, redis_client_t* rc) {
    LATTE_LIB_LOG(LOG_ERROR, "set_protocol_error: %s", errstr);
}

/** 解析内联（非 RESP）协议
 * 输入: rc - Redis客户端指针
 * 返回: 0 表示解析成功，-1 表示需要更多数据或出错
 * 功能: 解析简单的单行命令格式（如 telnet 风格的命令）
 */
int process_inline_buffer(redis_client_t* rc){
    LATTE_LIB_LOG(LOG_INFO, "process_inline_buffer");
    char *newline;
    int argc, j, linefeed_chars = 1;  // 换行符数量，默认为 \n
    sds *argv, aux;
    size_t query_len;

    redis_server_t* server = (redis_server_t*)rc->client.server;
    /* 查找行尾 */
    newline = strchr(rc->client.querybuf + rc->client.qb_pos, '\n');

    /* 没有 \r\n 就无法处理 */
    if (newline == NULL) {
        if (sds_len(rc->client.querybuf)-rc->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
            add_reply_error(rc,"Protocol error: too big inline request");
            set_protocol_error("too big inline request",rc);
        }
        return -1;
    }
    /* 处理 \r\n 情况 */
    if (newline != rc->client.querybuf+rc->client.qb_pos && *(newline-1) == '\r')
        newline--, linefeed_chars++;  // 如果有 \r，则换行符数量为 2
    /* 分割输入缓冲区直到 \r\n */
    query_len = newline-(rc->client.querybuf+rc->client.qb_pos);
    aux = sds_new_len(rc->client.querybuf+rc->client.qb_pos,query_len);
    argv = sds_split_args(aux,&argc);  // 按空格分割参数
    sds_delete(aux);
    if (argv == NULL) {
        add_reply_error(rc, "Protocol error: unbalanced quotes in request");
        set_protocol_error("unbalanced quotes in inline request",rc);
        return -1;
    }
    /* 从节点的换行符可用于刷新最后的 ACK 时间。
     * 这对于从节点在加载大 RDB 文件时发送 ping 很有用。 */
    if (query_len == 0 && get_client_type(rc) == CLIENT_TYPE_SLAVE)
        rc->repl_ack_time = server->unixtime;
    /* 主节点绝不应该向我们发送内联协议来运行实际命令。如果发生这种情况，
     * 可能是由于 Redis 中的错误导致协议中出现某种不同步，
     * 例如由于 PSYNC 错误。
     *
     * 但有一个例外：主节点可能只向我们发送换行符以保持连接活跃。 */
    if (query_len != 0 && rc->flag & CLIENT_MASTER) {
        sds_free_splitres(argv,argc);
        LATTE_LIB_LOG(LOG_WARN,"WARNING: Receiving inline protocol from master, master stream corruption? Closing the master connection and discarding the cached master.");
        set_protocol_error("Master using the inline protocol. Desync?",rc);
        return -1;
    }

    /* 将 querybuffer 位置移动到缓冲区中的下一个查询 */
    rc->client.qb_pos += query_len + linefeed_chars;

    /* 在客户端结构上设置 argv 数组 */
    if (argc) {
        if (rc->argv) zfree(rc->argv);
        rc->argv = zmalloc(sizeof(latte_object_t*)*argc);
        rc->argv_len_sum = 0;
    }
    /* 为所有参数创建 redis 对象 */
    for (rc->argc = 0, j = 0; j < argc; j++) {
        rc->argv[rc->argc] = latte_object_string_new(argv[j]);
        rc->argc++;
        rc->argv_len_sum += sds_len(argv[j]);
    }
    zfree(argv);
    return 0;
}

/** 鉴权检查（当前直接返回0，表示不需要鉴权）
 * 输入: c - Redis客户端指针
 * 返回: 0 表示不需要鉴权，非0表示需要鉴权
 */
int auth_required(redis_client_t *c) {
    /* 检查用户是否已认证。如果默认用户被标记为 "nopass" 并且是活动的，则跳过此检查。 */
    // int auth_required = (!(DefaultUser->flags & USER_FLAG_NOPASS) ||
    //                       (DefaultUser->flags & USER_FLAG_DISABLED)) &&
    //                     !c->authenticated;
    // return auth_required;
    return 0;  // 当前实现：始终允许访问
}
/** 解析 RESP 多批量协议（核心协议解析函数）
 * 输入: rc - Redis客户端指针
 * 返回: 0 表示解析完成，-1 表示需要更多数据或出错
 * 功能: 解析 Redis 标准的 RESP 协议格式
 *       格式: *<参数数量>\r\n$<参数长度>\r\n<参数内容>\r\n...
 *       例如: *2\r\n$3\r\nSET\r\n$5\r\nkey123\r\n
 */
int process_multibulk_buffer(redis_client_t* rc) {
    char* newline = NULL;
    int ok;
    long long ll;

    redis_server_t* server = (redis_server_t*)rc->client.server;

    /* 第一阶段：解析多批量长度（*N 部分） */
    if (rc->multi_bulk_len == 0) {
        /* 客户端应该已经被重置 */
        latte_assert_with_info(rc->argc == 0, "client argc ! = 0");
        /* 没有 \r\n 就无法读取多批量长度 */
        newline = strchr(rc->client.querybuf+rc->client.qb_pos,'\r');
        if (newline == NULL) {
            if (sds_len(rc->client.querybuf)-rc->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
                add_reply_error(rc,"Protocol error: too big mbulk count string");
                set_protocol_error("too big mbulk count string",rc);
            }
            return -1;  // 需要更多数据
        }

        /* 缓冲区还应该包含 \n */
        if (newline-(rc->client.querybuf+rc->client.qb_pos) > (ssize_t)(sds_len(rc->client.querybuf)-rc->client.qb_pos-2))
            return -1;

        /* 确定有完整的行，继续解析多批量长度 */
        latte_assert_with_info(rc->client.querybuf[rc->client.qb_pos] == '*', "querybuf[0] != '*'");
        // 解析 *N 中的 N（跳过 * 字符）
        ok = string2ll(rc->client.querybuf + 1 + rc->client.qb_pos, newline - (rc->client.querybuf+1+rc->client.qb_pos),&ll);
        if (!ok || ll > 1024*1024) {  // 参数数量不能超过 1M
            add_reply_error(rc,"Protocol error: invalid multibulk length");
            set_protocol_error("invalid mbulk count",rc);
            return -1;
        } else if (ll > 10 && auth_required(rc)) {  // 未认证时限制参数数量
            add_reply_error(rc, "Protocol error: unauthenticated multibulk length");
            set_protocol_error("unauth mbulk count", rc);
            return -1;
        }

        rc->client.qb_pos = (newline-rc->client.querybuf) + 2;  // 跳过 \r\n

        if (ll <= 0) return 0;  // 空命令或无效命令

        rc->multi_bulk_len = ll;  // 设置期望的参数数量

        /* 分配参数数组 */
        if (rc->argv) zfree(rc->argv);
        rc->argv = zmalloc(sizeof(latte_object_t*)*rc->multi_bulk_len);
        rc->argv_len_sum = 0;
    }

    /* 第二阶段：逐个解析每个批量字符串参数（$N 部分） */
    latte_assert_with_info(rc->multi_bulk_len > 0, "rc->multibulklen = %d ", rc->multi_bulk_len);
    while(rc->multi_bulk_len) {
        /* 如果批量长度未知，先读取批量长度 */
        if (rc->bulk_len == -1) {
            newline = strchr(rc->client.querybuf+rc->client.qb_pos,'\r');
            if (newline == NULL) {
                if (sds_len(rc->client.querybuf)-rc->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
                    add_reply_error(rc,
                        "Protocol error: too big bulk count string");
                    set_protocol_error("too big bulk count string",rc);
                    return -1;
                }
                break;  // 需要更多数据
            }

            /* 缓冲区还应该包含 \n */
            if (newline-(rc->client.querybuf+rc->client.qb_pos) > (ssize_t)(sds_len(rc->client.querybuf)-rc->client.qb_pos-2))
                break;

            /* 检查 $ 标记 */
            if (rc->client.querybuf[rc->client.qb_pos] != '$') {
                add_reply_error_format(rc,
                    "Protocol error: expected '$', got '%c'",
                    rc->client.querybuf[rc->client.qb_pos]);
                set_protocol_error("expected $ but got something else",rc);
                return -1;
            }

            /* 解析 $N 中的 N（参数长度） */
            ok = string2ll(rc->client.querybuf+rc->client.qb_pos+1,newline-(rc->client.querybuf+rc->client.qb_pos+1),&ll);
            if (!ok || ll < 0 ||
                (!(rc->flag & CLIENT_MASTER) && ll >
                server->proto_max_bulk_len)) {  // 检查参数长度限制
                add_reply_error(rc,"Protocol error: invalid bulk length");
                set_protocol_error("invalid bulk length",rc);
                return -1;
            } else if (ll > 16384 && auth_required(rc)) {  // 未认证时的长度限制
                add_reply_error(rc, "Protocol error: unauthenticated bulk length");
                set_protocol_error("unauth bulk length", rc);
                return -1;
            }

            rc->client.qb_pos = newline-rc->client.querybuf+2;  // 跳过长度行的 \r\n

            /* 大对象优化：如果要从网络读取大对象，尝试使其在 c->querybuf 边界开始，
             * 以便优化对象创建，避免大量数据复制 */
            if (ll >= PROTO_MBULK_BIG_ARG) {
                if (sds_len(rc->client.querybuf)-rc->client.qb_pos <= (size_t)ll+2) {
                    sds_range(rc->client.querybuf,rc->client.qb_pos,-1);  // 裁剪缓冲区
                    rc->client.qb_pos = 0;
                    /* 提示 sds 库此字符串将包含的字节数 */
                    rc->client.querybuf = sds_make_room_for(rc->client.querybuf,ll+2-sds_len(rc->client.querybuf));
                }
            }
            rc->bulk_len = ll;  // 设置当前参数的期望长度
        }

        /* 第三阶段：读取批量参数内容 */
        if (sds_len(rc->client.querybuf)-rc->client.qb_pos < (size_t)(rc->bulk_len+2)) {
            /* 数据不足（+2 表示尾部的 \r\n） */
            break;
        } else {
            /* 优化：如果缓冲区恰好包含我们的批量元素，
             * 不通过*复制* sds 来创建新对象，而是直接使用当前 sds 字符串 */
            if (rc->client.qb_pos == 0 &&
                rc->bulk_len >= PROTO_MBULK_BIG_ARG &&
                sds_len(rc->client.querybuf) == (size_t)(rc->bulk_len+2))
            {
                /* 零复制优化：直接使用整个缓冲区作为参数 */
                rc->argv[rc->argc++] = latte_object_string_new(rc->client.querybuf);
                rc->argv_len_sum += rc->bulk_len;
                sds_incr_len(rc->client.querybuf,-2);  // 移除 CRLF
                /* 假设如果看到一个大参数，可能还会看到另一个... */
                rc->client.querybuf = sds_new_len(SDS_NOINIT,rc->bulk_len+2);
                sds_clear(rc->client.querybuf);
            } else {
                /* 常规情况：复制参数内容到新对象 */
                rc->argv[rc->argc++] =
                    latte_object_string_new(sds_new_len(rc->client.querybuf+rc->client.qb_pos,rc->bulk_len));
                rc->argv_len_sum += rc->bulk_len;
                rc->client.qb_pos += rc->bulk_len+2;  // 跳过内容和 \r\n
            }
            rc->bulk_len = -1;      // 重置为未知状态
            rc->multi_bulk_len--;   // 减少剩余参数计数
        }
    }

    /* 当 c->multibulk == 0 时表示完成 */
    if (rc->multi_bulk_len == 0) return 0;

    /* 仍未准备好处理命令 */
    return -1;
}





/** 循环处理输入缓冲区中的数据
 * 输入: rc - Redis客户端指针
 * 返回: 0 表示客户端已死亡，1 表示成功处理
 * 功能: 持续解析输入缓冲区中的命令并执行，直到缓冲区为空或遇到需要更多数据的情况
 */
int redis_process_input_buffer(redis_client_t* rc) {
    LATTE_LIB_LOG(LOG_INFO, "redis_process_input_buffer");
    //解析读取的数据 转换成object 对象
    /* 当输入缓冲区中还有内容时继续处理 */
    while (rc->client.qb_pos < sds_len(rc->client.querybuf)) {
        /* 如果客户端处于某种阻塞状态，立即中止 */
        //if (rc->flag & CLIENT_BLOCKED break;

        /* 如果客户端正在交换，也中止 */
        //if (rc->flags & CLIENT_SWAPPING || c->flags & CLIENT_SWAP_REWINDING) break;

        /* 不处理已有待执行命令在 c->argv 中的客户端的更多缓冲区 */
        // if (c->flags & CLIENT_PENDING_COMMAND) break;

        /* 当从节点上有繁忙脚本条件时，不处理来自主节点的输入。
         * 我们只想累积复制流（而不是像其他客户端那样回复 -BUSY）并稍后恢复处理。 */
        //if (server.lua_timedout && rc->flags & CLIENT_MASTER) break;

        /* CLIENT_CLOSE_AFTER_REPLY 在回复写入客户端后关闭连接。
         * 确保在设置此标志后不让回复增长（即不处理更多命令）。
         * 对于我们想要尽快终止的客户端也是如此。 */
        if (rc->client.flags & (CLIENT_CLOSE_AFTER_REPLY|CLIENT_CLOSE_ASAP)) {
            LATTE_LIB_LOG(LOG_INFO, "redis_process_input_buffer CLIENT_CLOSE_AFTER_REPLY|CLIENT_CLOSE_ASAP break");
            break;
        }

        /* 判断协议类型：如果还未确定协议类型 */
        if (!rc->req_type) {
            if (rc->client.querybuf[rc->client.qb_pos] == '*') { //判定协议如果开头是* 就是redis协议
                rc->req_type = PROTO_REQ_MULTIBULK;  // RESP 多批量协议
            } else {
                rc->req_type = PROTO_REQ_INLINE;     // 内联协议
            }
        }

        /* 根据协议类型选择相应的解析函数 */
        if (rc->req_type == PROTO_REQ_INLINE) { //普通协议
            if (process_inline_buffer(rc) != 0) break;
            /* 如果 Gopher 模式且我们得到零个或一个参数，在 Gopher 模式下处理请求。
             * 为避免数据竞争，如果启用 io 线程读取查询，Redis 不支持 Gopher。 */
        //     if (server.gopher_enabled && !server.io_threads_do_reads &&
        //         ((c->argc == 1 && ((char*)(c->argv[0]->ptr))[0] == '/') ||
        //           c->argc == 0))
        //     {
        //         processGopherRequest(c);
        //         resetClient(c);
        //         c->flags |= CLIENT_CLOSE_AFTER_REPLY;
        //         break;
        //     }
        } else if (rc->req_type == PROTO_REQ_MULTIBULK) { //redis协议
            if (process_multibulk_buffer(rc) != 0) break;
        } else {
            redis_panic("Unknown request type");  // 未知的请求类型
        }

        /* 多批量处理可能会看到 <= 0 的长度 */
        if (rc->argc == 0) {
            reset_redis_client(rc);  // 重置客户端以处理下一个命令
        } else {
            /* 如果我们在 I/O 线程的上下文中，我们实际上无法在这里执行命令。
             * 我们所能做的就是将客户端标记为需要处理命令的客户端。 */
            // if (rc->flags & CLIENT_PENDING_READ) {
            //     rc->flags |= CLIENT_PENDING_COMMAND;
            //     break;
            // }

            /* 我们终于准备好执行命令了 */
            if (process_command_and_reset_client(rc) == -1) {
                /* 如果客户端不再有效，我们避免退出此循环并稍后修剪客户端缓冲区。
                 * 所以我们在这种情况下尽快返回。 */
                return 0;
            }
        }
    }

    if (rc->client.qb_pos) return 1;
    LATTE_LIB_LOG(LOG_INFO, "redis_process_input_buffer end");
    return 0;
}
/** 客户端数据到达时的处理回调函数
 * 输入: lc - 基础客户端指针, nread - 读取的字节数
 * 返回: 处理结果
 * 功能: 1. 更新复制偏移量（如果是主节点客户端）
 *       2. 调用 redis_process_input_buffer 处理输入数据
 */
int redis_client_handle(struct latte_client_t* lc, int nread) {
    redis_client_t* rc = (redis_client_t*)lc;
    redis_server_t* server = (redis_server_t*)lc->server;
    //rc->lastinteraction = lc->server.unixtime;  // 更新最后交互时间
    if (rc->flag & CLIENT_MASTER) {
        rc->read_reploff += nread;  // 更新从主节点读取的复制偏移量
    }
    // latte_atomic_incr(server.stat_net_input_bytes, nread); //统计读取数据
    // if (sds_len(lc->querybuf) > server.client_max_querybuf_len) { //如果读取数据大于每次请求次数的时候 返回错误断开客户端
    //     sds ci = catClientInfoString(sdsempty(),c), bytes = sdsempty();
    //
    //     bytes = sdscatrepr(bytes,c->querybuf,64);
    //     serverLog(LOG_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
    //     sdsfree(ci);
    //     sdsfree(bytes);
    //     freeClientAsync(c);
    //     return;
    // }
    /* 客户端输入缓冲区中有更多数据，继续解析以检查是否有完整的命令要执行 */
    return redis_process_input_buffer(rc);
}

/** 命令执行结束后的回调函数
 * 输入: cli - 基础客户端指针
 * 功能: 1. slowlog 记录（如果需要）
 *       2. QPS 统计更新
 */
void redis_client_command_end(latte_client_t* cli) {
    redis_client_t* rc = (redis_client_t*)cli;
    redis_server_t* server = (redis_server_t*)cli->server;

    /* slowlog 慢日志记录 */
    slowlog_manager_push_if_needed(server->slowlog_manager,rc);
    /* QPS 监控统计 */
    server->metric_stat_numcommands++;
}

/** 创建 Redis 客户端对象
 * 返回: 新创建的客户端实例
 * 功能: 初始化 Redis 客户端的所有字段和回调函数
 */
latte_client_t* create_redis_client() {
    redis_client_t* redis_client = zmalloc(sizeof(redis_client_t));
    redis_client->client.exec = redis_client_handle;        // 设置数据处理回调
    redis_client->dbid = 0;                                 // 默认数据库 ID
    redis_client->multi_bulk_len = 0;                       // 多批量长度初始化
    redis_client->bulk_len = -1;                            // 非常重要！Redis协议命令解析用
    redis_client->current_decode_time = 0;                  // 解码时间初始化
    redis_client->current_encode_time = 0;                  // 编码时间初始化
    redis_client->current_call_time = 0;                    // 调用时间初始化
    redis_client->client.start = NULL;                      // 开始回调
    redis_client->client.end = redis_client_command_end;    // 结束回调
    redis_client->client.start_time = 0;                    // 开始时间
    redis_client->client.read_time = 0;                     // 读取时间
    redis_client->client.exec_time = 0;                     // 执行时间
    redis_client->client.exec_end_time = 0;                 // 执行结束时间
    redis_client->client.write_time = 0;                    // 写入时间
    redis_client->client.end_time = 0;                      // 结束时间
    redis_client->client.flags = 0;                         // 客户端标志
    return (latte_client_t*)redis_client;
}

/** 删除 Redis 客户端
 * 输入: client - 要删除的客户端
 * 功能: 释放客户端占用的内存
 */
void redis_client_delete(latte_client_t* client) {
    redis_client_t* redis_client = (redis_client_t*)client;
    zfree(redis_client);
}


/** 释放 Redis 客户端内存
 * 输入: rc - 要释放的 Redis 客户端
 * 功能: 完整清理客户端相关资源
 */
void free_redis_client(redis_client_t *rc) {
    list_node_t *ln;

    redis_server_t* server = (redis_server_t*)rc->client.server;
    /* 从 server.repl_swapping_clients 中取消链接复制客户端 */
    // repl_client_discard_swapping_state(c);

    /* 如果客户端受保护，但我们现在需要释放它，确保至少使用异步释放 */
    // if (c->flags & CLIENT_PROTECTED || c->flags & CLIENT_SWAP_UNLOCKING) {
    //     free_redis_client_async(c);
    //     return;
    // }

    /* 对于连接的客户端，调用模块钩子的断连事件 */
    // if (c->conn) {
    //     moduleFireServerEvent(REDISMODULE_EVENT_CLIENT_CHANGE,
    //                           REDISMODULE_SUBEVENT_CLIENT_CHANGE_DISCONNECTED,
    //                           c);
    // }

    /* 通知模块系统此客户端的认证状态已更改 */
    // moduleNotifyUserChanged(c);

    /* 如果此客户端被安排异步释放，我们需要从队列中删除它。
     * 注意我们需要在这里做这件事，因为稍后我们可能会调用 replicationCacheMaster()，
     * 客户端应该已经从要释放的客户端列表中删除。 */
    if (rc->client.flags & CLIENT_CLOSE_ASAP) {
        ln = list_search_key(server->clients_to_close,rc);
        latte_assert(ln != NULL);
        list_del_node(server->clients_to_close,ln);
    }


}

/** 安排客户端在 serverCron() 函数中的安全时机释放
 * 输入: rc - 要异步释放的 Redis 客户端
 * 功能: 当我们需要终止客户端但处于无法调用 freeClient() 的上下文中时，
 *       此函数很有用，因为客户端应该对程序流程的继续有效
 */
void free_redis_client_async(redis_client_t* rc) {
    /* 我们需要处理对 server.clients_to_close 列表的并发访问，
     * 只在 freeClientAsync() 函数中，因为它是唯一可能在 Redis 使用 I/O 线程时
     * 访问列表的函数。所有其他访问都在主线程的上下文中，而其他线程处于空闲状态。 */
    if (rc->client.flags & CLIENT_CLOSE_ASAP || rc->flag & CLIENT_LUA) return;
    rc->client.flags |= CLIENT_CLOSE_ASAP;
    redis_server_t* server = (redis_server_t*)rc->client.server;

    { // 暂时默认 io_threads_num 为 1
        list_add_node_tail(server->clients_to_close,rc);
        return;
    }
    // if (server.io_threads_num == 1) {
    //     /* 如果只有一个线程（主线程），则无需费心加锁 */
    //     list_add_node_tail(server.clients_to_close,rc);
    //     return;
    // }
    // 为什么别的地方不需要加锁（比如删除客户端）只有这里追加到最后的客户端需要加锁
    // static pthread_mutex_t async_free_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
    // pthread_mutex_lock(&async_free_queue_mutex);
    // list_add_node_tail(server->clients_to_close, rc);
    // pthread_mutex_unlock(&async_free_queue_mutex);
}

