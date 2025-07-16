#include "client.h"
#include "error/error.h"
#include "object/object.h"
#include "object/string.h"
#include "string.h"
#include "utils/utils.h"
#include "utils/monotonic.h"
#include "debug/latte_debug.h"

/* Client request types */
#define PROTO_REQ_INLINE 1
#define PROTO_REQ_MULTIBULK 2

#define PROTO_MBULK_BIG_ARG     (1024*32)
//一行最多字符数
#define PROTO_INLINE_MAX_SIZE (1024*64)

latte_error_t* processInlineBuffer(redis_client_t* c) {
    char* new_line;
    int argc, j, linefeed_chars = 1;
    sds *argv, aux;
    size_t query_len;

    //找到一行的结尾
    new_line = strchr(c->client.querybuf + c->client.qb_pos, '\n');

    if (new_line == NULL) {
        if (sds_len(c->client.querybuf) - c->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
            addReplyError(c,"Protocol error: too big inline request");
            // setProtocolError("too big inline request",c);
        }
        return error_new(CInvalidArgument, "too big inline request", "");
    }

    //处理\r\n
    if (new_line != c->client.querybuf + c ->client.qb_pos && *(new_line-1) == '\r') 
        new_line--, linefeed_chars++;
    
    query_len = new_line - (c->client.querybuf + c->client.qb_pos);
    aux = sds_new_len(c->client.querybuf + c ->client.qb_pos, query_len);
    argv = sds_split_args(aux, &argc);
    sds_delete(aux);
    if (argv == NULL) {
        addReplyError(c,"Protocol error: unbalanced quotes in request");
        // setProtocolError("unbalanced quotes in inline request",c);
        return error_new(CInvalidArgument, "unbalanced quotes in inline request", "");
    }
    /* 可以使用从属设备的换行符来刷新最后的 ACK 时间。
    * 这对于从属设备在加载大型
    * RDB 文件时进行 ping 操作非常有用。 */
    // if (query_len == 0 && getClientType(c) == CLIENT_TYPE_SLAVE)
    //     c->repl_ack_time = redisServer.unixtime;

    /* 主服务器永远不应向我们发送内联协议来运行实际的
    * 命令。如果发生这种情况，很可能是由于 Redis 中的一个错误，例如，由于 PSYNC 出现故障，导致协议中出现一些不同步。
    *
    * 但是有一个例外：主服务器可能只向我们发送换行符
    * 以保持连接处于活动状态。 */
    if (query_len != 0 && c->flags & CLIENT_MASTER) {
        sds_free_splitres(argv,argc);
        LATTE_LIB_LOG(LOG_WARN, "WARNING: Receiving inline protocol from master, master stream corruption? Closing the master connection and discarding the cached master.");
        // setProtocolError("Master using the inline protocol. Desync?",c);
        return error_new(CInvalidArgument, "Master using the inline protocol. Desync?", "");
    }

    c->client.qb_pos += query_len + linefeed_chars;

    if (argc) {
        if (c->argv) zfree(c->argv);
        c->argv = zmalloc(sizeof(latte_object_t*)*argc);
        c->argv_len_sum = 0;
    }

    for (c->argc = 0, j = 0; j < argc; j++) {
        c->argv[c->argc] = latte_object_new(OBJ_STRING, argv[j]);
        c->argc++;
        c->argv_len_sum += sds_len(argv[j]);
    }
    zfree(argv);
    return &Ok;
}

static latte_error_t NEED_NEXT = {COk, NULL};
/* 处理客户端“c”的查询缓冲区，设置客户端参数
* 向量以执行命令。如果运行该函数后
* 客户端具有格式良好的待处理命令，则返回 0，否则
* 如果仍需读取更多缓冲区以获取完整命令，则返回 -1。
* 当出现协议错误时，该函数还会返回 -1：在这种情况下
* 客户端结构设置为回复错误并关闭
* 连接。
*
* 如果 processInputBuffer() 检测到下一个
* 命令为 RESP 格式，则调用此函数，因此命令中的第一个字节
* 为“*”。否则，对于内联命令，将调用 processInlineBuffer()。*/
latte_error_t* processMultibulkBuff(redis_client_t *c) {
    if (NEED_NEXT.state == NULL) {
        NEED_NEXT.state = sds_new("need next");
    }
    char *newline = NULL;
    int ok;
    long long ll;

    if (c->multibulklen == 0) {
        /* The client should have been reset */
        // serverAssertWithInfo(c,NULL,c->argc == 0);

        /* Multi bulk length cannot be read without a \r\n */
        newline = strchr(c->client.querybuf+c->client.qb_pos,'\r');
        if (newline == NULL) {
            if (sds_len(c->client.querybuf)-c->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
                addReplyError(c,"Protocol error: too big mbulk count string");
                // setProtocolError("too big mbulk count string",c);
            }
            return error_new(CInvalidArgument, "too big mbulk count string", "");
        }

        /* 缓冲液还应含有\n */
        if (newline-(c->client.querybuf+c->client.qb_pos) > (ssize_t)(sds_len(c->client.querybuf)-c->client.qb_pos-2))
            return error_new(CInvalidArgument, "size error", "");;

        /* 我们肯定知道有一整行，因为换行符 != NULL,
            * 所以继续找出多行长度。 */
        // serverAssertWithInfo(c,NULL,c->client.querybuf[c->client.qb_pos] == '*');
        ok = string2ll(c->client.querybuf+1+c->client.qb_pos,newline-(c->client.querybuf+1+c->client.qb_pos),&ll);
        if (!ok || ll > 1024*1024) {
            addReplyError(c,"Protocol error: invalid multibulk length");
            return error_new(CInvalidArgument, "invalid mbulk count", "");
        } else if (ll > 10 ) { //&& authRequired(c)
            addReplyError(c, "Protocol error: unauthenticated multibulk length");
            return error_new(CInvalidArgument, "unauth mbulk count", "");
        }

        c->client.qb_pos = (newline-c->client.querybuf)+2;

        if (ll <= 0) return &Ok;

        c->multibulklen = ll;

        /* Setup argv array on client structure */
        if (c->argv) zfree(c->argv);
        c->argv = zmalloc(sizeof(latte_object_t*)*c->multibulklen);
        c->argv_len_sum = 0;
    }
    // serverAssertWithInfo(c,NULL,c->multibulklen > 0);
    while(c->multibulklen) {
        /* Read bulk length if unknown */
        if (c->bulklen == -1) {
            newline = strchr(c->client.querybuf+c->client.qb_pos,'\r');
            if (newline == NULL) {
                if (sds_len(c->client.querybuf)-c->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
                    addReplyError(c,
                        "Protocol error: too big bulk count string");
                    // setProtocolError("too big bulk count string",c);
                    return error_new(CInvalidArgument, "too big bulk count string", "");
                }
                break;
            }

            /* Buffer should also contain \n */
            if (newline-(c->client.querybuf+c->client.qb_pos) > (ssize_t)(sds_len(c->client.querybuf)-c->client.qb_pos-2))
                break;

            if (c->client.querybuf[c->client.qb_pos] != '$') {
                addReplyErrorFormat(c,
                    "Protocol error: expected '$', got '%c'",
                    c->client.querybuf[c->client.qb_pos]);
                // setProtocolError("expected $ but got something else",c);
                return error_new(CInvalidArgument, "expected $ but got something else", "");
            }

            ok = string2ll(c->client.querybuf+c->client.qb_pos+1,newline-(c->client.querybuf+c->client.qb_pos+1),&ll);
            if (!ok || ll < 0 ||
                (!(c->flags & CLIENT_MASTER) && ll > redisServer.proto_max_bulk_len)) {
                addReplyError(c,"Protocol error: invalid bulk length");
                return error_new(CInvalidArgument, "invalid bulk length", "");
            } else if (ll > 16384 ) { //&& authRequired(c)
                addReplyError(c, "Protocol error: unauthenticated bulk length");
                return error_new(CInvalidArgument, "unauth bulk length", "");
            }

            c->client.qb_pos = newline-c->client.querybuf+2;
            if (ll >= PROTO_MBULK_BIG_ARG) {
                /* If we are going to read a large object from network
                 * try to make it likely that it will start at c->client.querybuf
                 * boundary so that we can optimize object creation
                 * avoiding a large copy of data.
                 *
                 * But only when the data we have not parsed is less than
                 * or equal to ll+2. If the data length is greater than
                 * ll+2, trimming querybuf is just a waste of time, because
                 * at this time the querybuf contains not only our bulk. */
                if (sds_len(c->client.querybuf)-c->client.qb_pos <= (size_t)ll+2) {
                    sds_range(c->client.querybuf,c->client.qb_pos,-1);
                    c->client.qb_pos = 0;
                    /* Hint the sds library about the amount of bytes this string is
                     * going to contain. */
                    c->client.querybuf = sds_make_room_for(c->client.querybuf,ll+2-sds_len(c->client.querybuf));
                }
            }
            c->bulklen = ll;
        }

        /* Read bulk argument */
        if (sds_len(c->client.querybuf)-c->client.qb_pos < (size_t)(c->bulklen+2)) {
            /* Not enough data (+2 == trailing \r\n) */
            break;
        } else {
            /* Optimization: if the buffer contains JUST our bulk element
             * instead of creating a new object by *copying* the sds we
             * just use the current sds string. */
            if (c->client.qb_pos == 0 &&
                c->bulklen >= PROTO_MBULK_BIG_ARG &&
                sds_len(c->client.querybuf) == (size_t)(c->bulklen+2))
            {
                c->argv[c->argc++] = latte_object_new(OBJ_STRING,c->client.querybuf);
                c->argv_len_sum += c->bulklen;
                sds_inc_len(c->client.querybuf,-2); /* remove CRLF */
                /* Assume that if we saw a fat argument we'll see another one
                 * likely... */
                c->client.querybuf = sds_new_len(SDS_NOINIT,c->bulklen+2);
                sds_clear(c->client.querybuf);
            } else {
                c->argv[c->argc++] =
                    string_object_new(c->client.querybuf+c->client.qb_pos,c->bulklen);
                c->argv_len_sum += c->bulklen;
                c->client.qb_pos += c->bulklen+2;
            }
            c->bulklen = -1;
            c->multibulklen--;
        }
    }

    /* We're done when c->multibulk == 0 */
    if (c->multibulklen == 0) return &Ok;

    /* Still not ready to process the command */
    return &NEED_NEXT;
}

static void freeClientArgv(redis_client_t *c) {
    int j;
    for (j = 0; j < c->argc; j++)
        latte_object_decr_ref_count(c->argv[j]);
    c->argc = 0;
    c->cmd = NULL;
    c->argv_len_sum = 0;
}

/* resetClient prepare the client to process the next command */
void resetClient(redis_client_t *c) {
    redis_command_proc *prevcmd = c->cmd ? c->cmd->proc : NULL;

    freeClientArgv(c);
    c->reqtype = 0;
    c->multibulklen = 0;
    c->bulklen = -1;

    if (c->deferred_reply_errors)
        list_delete(c->deferred_reply_errors);
    c->deferred_reply_errors = NULL;

    /* We clear the ASKING flag as well if we are not inside a MULTI, and
     * if what we just executed is not the ASKING command itself. */
    // if (!(c->flags & CLIENT_MULTI) && prevcmd != askingCommand)
    //     c->flags &= ~CLIENT_ASKING;

    /* We do the same for the CACHING command as well. It also affects
     * the next command or transaction executed, in a way very similar
     * to ASKING. */
    // if (!(c->flags & CLIENT_MULTI) && prevcmd != clientCommand)
    //     c->flags &= ~CLIENT_TRACKING_CACHING;

    /* Remove the CLIENT_REPLY_SKIP flag if any so that the reply
     * to the next command will be sent, but set the flag if the command
     * we just processed was "CLIENT REPLY SKIP". */
    c->flags &= ~CLIENT_REPLY_SKIP;
    if (c->flags & CLIENT_REPLY_SKIP_NEXT) {
        c->flags |= CLIENT_REPLY_SKIP;
        c->flags &= ~CLIENT_REPLY_SKIP_NEXT;
    }
}

/* Command call flags, see call() function */
#define CMD_CALL_NONE 0
#define CMD_CALL_PROPAGATE_AOF (1<<0)
#define CMD_CALL_PROPAGATE_REPL (1<<1)
#define CMD_CALL_REPROCESSING (1<<2)
#define CMD_CALL_FROM_MODULE (1<<3)  /* From RM_Call */
#define CMD_CALL_PROPAGATE (CMD_CALL_PROPAGATE_AOF|CMD_CALL_PROPAGATE_REPL)
#define CMD_CALL_FULL (CMD_CALL_PROPAGATE)

void call(redis_client_t *c, int flags) {
    const long long call_timer = ustime();
    monotime monotonic_start = 0;
    if (monotonicGetType() == MONOTONIC_CLOCK_HW)
        monotonic_start = getMonotonicUs();
    // server.in_nested_call++;
    c->cmd->proc(c);
    // server.in_nested_call--;

    /* In order to avoid performance implication due to querying the clock using a system call 3 times,
     * we use a monotonic clock, when we are sure its cost is very low, and fall back to non-monotonic call otherwise. */
    long long  duration;
    if (monotonicGetType() == MONOTONIC_CLOCK_HW)
        duration = getMonotonicUs() - monotonic_start;
    else
        duration = ustime() - call_timer;
    c->duration = duration;
    
}

void init_shared_strings() {
    static bool inited = false;
    if (inited == true) return;
    shared_strings.ok = latte_object_new(OBJ_STRING,sds_new("+OK\r\n"));
    inited = true;
}



int procesCommand(redis_client_t* c) {
    
    if (!strcasecmp(c->argv[0]->ptr, "quit")) {
        init_shared_strings();
        addReply(c, shared_strings.ok);
        c->flags |= CLIENT_CLOSE_AFTER_REPLY;
        return 0;
    }
    call(c, CMD_CALL_FULL);
    return 1; 
}

int processCommandAndResetClient(redis_client_t* c) {
    // int deadclient = 0;
    // latte_client_t *old_client = c.server.current_client;
    // c.server.current_client = c;
    if (procesCommand(c) == 1) {
        // commandProcessed(c);
        // if (c->conn) updateClientMemUsageAndBucket(c);
        return -1;
    }

    // if (c.server.current_client == NULL) deadclient = 1;

    // c.server.current_client = old_client;
    return 1 ;


}

/* 每次在客户端结构“c”中，都会调用此函数
* 处理更多的查询缓冲区，因为我们从套接字读取了更多数据
* 或者因为客户端被阻止并随后重新激活，因此可能会有
* 待处理的查询缓冲区（已代表完整的命令）需要处理。*/
int processInputBuffer(redis_client_t *c) {
    /* 当输入缓冲区中有内容时继续处理 */
    while(c->client.qb_pos < sds_len(c->client.querybuf)) {
        /* 如果客户端正在执行某项操作则立即中止。 */
        // if (c->flags & CLIENT_BLOCKED) break;
        /* 不要处理来自已经在 c->argv 中执行的待处理命令的客户端的更多缓冲区。 */
        // if (c->flags & CLIENT_PENDING_COMMAND) break;

        /* 当从属服务器上存在繁忙脚本
        * 情况时，不要处理来自主服务器的输入。我们只想累积复制
        * 流（而不是像对待其他客户端一样回复 -BUSY）并
        * 稍后恢复处理。*/
    //    if (server.lua_timedout && c->flags & CLIENT_MASTER) break;
        /* CLIENT_CLOSE_AFTER_REPLY 在回复写入客户端后关闭连接。请确保在设置此标志后不要让回复增长（即不要处理更多命令）。
        *
        * 这同样适用于我们希望尽快终止的客户端。 */
        // if (c->flags & (CLIENT_CLOSE_AFTER_REPLY|CLIENT_CLOSE_ASAP)) break;
        if (!c->reqtype) {
            if (c->client.querybuf[c->client.qb_pos] == '*') {
                c->reqtype = PROTO_REQ_MULTIBULK;
            } else {
                c->reqtype = PROTO_REQ_INLINE;
            }
        }

        if (c->reqtype == PROTO_REQ_INLINE) {
            if (! error_is_ok(processInlineBuffer(c)) ) break;
            /* 如果 Gopher 模式并且我们得到零个或一个参数，则以 Gopher 模式处理请求。为避免数据竞争，如果启用 io 线程来读取查询，Redis 将不支持 Gopher。*/
            // if (server.gopher_enabled && !server.io_threads_do_reads &&
            //     ((c->argc == 1 && ((char*)(c->argv[0]->ptr))[0] == '/') ||
            //       c->argc == 0))
            // {
            //     processGopherRequest(c);
            //     resetClient(c);
            //     c->flags |= CLIENT_CLOSE_AFTER_REPLY;
            //     break;
            // }
        } else if (c->reqtype == PROTO_REQ_MULTIBULK) {
            if ( !error_is_ok(processMultibulkBuff(c))) break;
        } else {
            LATTE_LIB_LOG(LOG_ERROR, "Unknown request type");
            exit(0);
        }

        if (c->argc == 0) {
            resetClient(c);
        } else {
            // if (c->flags & CLIENT_PENDING_READ) {
            //     c->flags |= CLIENT_PENDING_COMMAND;
            //     break;
            // }

            if (processCommandAndResetClient(c) == -1) {
                return 0;
            }
        }

       

    }
    return 1;
}

int redisReadHandler(struct latte_client_t* client) {
    redis_client_t* c = (redis_client_t*)client;
    // c->lastinteraction = server.unixtime;
    // if (c->flags & CLIENT_MASTER) c->read_reploff += nread;
    // if (sds_len(client->querybuf) >  redisServer.server.client_max_querybuf_len) {
    //     sds ci = catClientInfo(sds_empty(), c ), bytes = sdsempty();
    //     bytes = sdscatrepr(bytes,c->client.querybuf,64);
    //     serverLog(LL_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
    //     sdsfree(ci);
    //     sdsfree(bytes);
    //     freeClientAsync(c);
    //     return 0;
    // }
    /* 客户端输入缓冲区中还有更多数据，请继续解析
        * 以防检查是否有完整的命令需要执行。 */
    return processInputBuffer(c);
    

}

latte_client_t* createRedisClient() {
    redis_client_t* c = zmalloc(sizeof(redis_client_t));
    c->client.exec = redisReadHandler;
    c->deferred_reply_errors = list_new();
    return c;
}
void freeRedisClient(latte_client_t* client) {
    zfree(client);
}



/* -----------------------------------------------------------------------------
 * Low level functions to add more data to output buffers.
 * -------------------------------------------------------------------------- */

/* Attempts to add the reply to the static buffer in the client struct.
 * Returns -1 if the buffer is full, or the reply list is not empty,
 * in which case the reply must be added to the reply list. */
int _addReplyToBuffer(redis_client_t *c, const char *s, size_t len) {
    size_t available = sizeof(c->buf)-c->bufpos;

    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) return 0;

    /* If there already are entries in the reply list, we cannot
     * add anything more to the static buffer. */
    if (list_length(c->reply) > 0) return -1;

    /* Check that the buffer has enough space available for this string. */
    if (len > available) return -1;

    memcpy(c->buf+c->bufpos,s,len);
    c->bufpos+=len;
    return 0;
}


char *getClientTypeName(int class) {
    switch(class) {
    case CLIENT_TYPE_NORMAL: return "normal";
    case CLIENT_TYPE_SLAVE:  return "slave";
    case CLIENT_TYPE_PUBSUB: return "pubsub";
    case CLIENT_TYPE_MASTER: return "master";
    default:                       return NULL;
    }
}
/* This function returns the number of bytes that Redis is
 * using to store the reply still not read by the client.
 *
 * Note: this function is very fast so can be called as many time as
 * the caller wishes. The main usage of this function currently is
 * enforcing the client output length limits. */
unsigned long getClientOutputBufferMemoryUsage(redis_client_t *c) {
    unsigned long list_item_size = sizeof(list_node_t) + sizeof(client_reply_block_t);
    return c->reply_bytes + (list_item_size*list_length(c->reply));
}

/* Get the class of a client, used in order to enforce limits to different
 * classes of clients.
 *
 * The function will return one of the following:
 * CLIENT_TYPE_NORMAL -> Normal client
 * CLIENT_TYPE_SLAVE  -> Slave
 * CLIENT_TYPE_PUBSUB -> Client subscribed to Pub/Sub channels
 * CLIENT_TYPE_MASTER -> The client representing our replication master.
 */
int getClientType(redis_client_t *c) {
    if (c->flags & CLIENT_MASTER) return CLIENT_TYPE_MASTER;
    /* Even though MONITOR clients are marked as replicas, we
     * want the expose them as normal clients. */
    if ((c->flags & CLIENT_SLAVE) && !(c->flags & CLIENT_MONITOR))
        return CLIENT_TYPE_SLAVE;
    if (c->flags & CLIENT_PUBSUB) return CLIENT_TYPE_PUBSUB;
    return CLIENT_TYPE_NORMAL;
}

/* The function checks if the client reached output buffer soft or hard
 * limit, and also update the state needed to check the soft limit as
 * a side effect.
 *
 * Return value: non-zero if the client reached the soft or the hard limit.
 *               Otherwise zero is returned. */
// int checkClientOutputBufferLimits(redis_client_t *c) {
//     int soft = 0, hard = 0, class;
//     unsigned long used_mem = getClientOutputBufferMemoryUsage(c);

//     class = getClientType(c);
//     /* For the purpose of output buffer limiting, masters are handled
//      * like normal clients. */
//     if (class == CLIENT_TYPE_MASTER) class = CLIENT_TYPE_NORMAL;

//     if (redisServer.client_obuf_limits[class].hard_limit_bytes &&
//         used_mem >= server.client_obuf_limits[class].hard_limit_bytes)
//         hard = 1;
//     if (redisServer.client_obuf_limits[class].soft_limit_bytes &&
//         used_mem >= server.client_obuf_limits[class].soft_limit_bytes)
//         soft = 1;

//     /* We need to check if the soft limit is reached continuously for the
//      * specified amount of seconds. */
//     if (soft) {
//         if (c->obuf_soft_limit_reached_time == 0) {
//             c->obuf_soft_limit_reached_time = server.unixtime;
//             soft = 0; /* First time we see the soft limit reached */
//         } else {
//             time_t elapsed = server.unixtime - c->obuf_soft_limit_reached_time;

//             if (elapsed <=
//                 server.client_obuf_limits[class].soft_limit_seconds) {
//                 soft = 0; /* The client still did not reached the max number of
//                              seconds for the soft limit to be considered
//                              reached. */
//             }
//         }
//     } else {
//         c->obuf_soft_limit_reached_time = 0;
//     }
//     return soft || hard;
// }

// int closeClientOnOutputBufferLimitReached(redis_client_t *c, int async) {
//     if (!c->client.conn) return 0; /* It is unsafe to free fake clients. */
//     redisAssert(c->reply_bytes < SIZE_MAX-(1024*64));
//     if (c->reply_bytes == 0 || c->flags & CLIENT_CLOSE_ASAP) return 0;
//     if (checkClientOutputBufferLimits(c)) {
//         sds client = catClientInfoString(sdsempty(),c);

//         if (async) {
//             freeRedisClientAsync(c);
//             // REDIS_LOG(,
//             //           "Client %s scheduled to be closed ASAP for overcoming of output buffer limits.",
//             //           client);
//         } else {
//             freeRedisClient(c);
//             // REDIS_LOG(LOG_WARN,
//             //           "Client %s closed for overcoming of output buffer limits.",
//             //           client);
//         }
//         sds_delete(client);
//         return  1;
//     }
//     return 0;
// }

/* 此函数将客户端放入应将其输出缓冲区写入套接字的客户端队列中。
请注意，它尚未安装写入处理程序，
因此，要启动客户端，请将其放入需要写入的客户端队列中，
因此我们会在事件循环返回之前尝试执行此操作（请参阅 handleClientsWithPendingWrites() 函数）。
如果我们失败并且要写入的数据比套接字缓冲区可以容纳的数据多，那么我们才会真正安装处理程序。*/
void clientInstallWriteHandler(redis_client_t *c) {
    /* 安排客户端仅将输出缓冲区写入套接字
    * 如果尚未完成，并且对于从属设备，如果从属设备实际上可以接收
    * 在此阶段写入。*/
    if (!(c->flags & CLIENT_PENDING_WRITE) 
    // &&
    //     (c->replstate == REPL_STATE_NONE ||
    //      (c->replstate == SLAVE_STATE_ONLINE && !c->repl_put_online_on_ack))
         )
    {
        /* 在这里，我们无需安装写入处理程序，
        只需标记客户端并将其放入有内容可写入套接字的客户端列表中即可。
        这样，在重新进入事件循环之前，我们可以尝试直接写入客户端套接字，
        从而避免系统调用。如果我们无法一次写入整个回复，我们才会真正安装写入处理程序。*/
        c->flags |= CLIENT_PENDING_WRITE;
        list_add_node_head(redisServer.clients_pending_write,c);
    }
}

/* 每次我们要向客户端传输新数据时，都会调用此函数。行为如下：
*
* 如果客户端应该接收新数据（普通客户端会接收），则函数返回 0，并确保在我们的事件循环中安装写入处理程序，这样当套接字可写时，新数据就会被写入。
*
* 如果客户端不应该接收新数据，因为它是假客户端（用于在内存中加载 AOF）、主客户端或因为写入处理程序的设置失败，则函数返回 -1。
*
* 在以下情况下，函数可能会返回 0 而不实际安装写入事件处理程序：
*
* 1) 事件处理程序应该已经安装，因为输出缓冲区已经包含某些内容。
* 2) 客户端是从属客户端，但尚未上线，因此我们只想在缓冲区中累积写入，但实际上尚未发送它们。
*
* 通常在每次构建回复时调用，然后再将更多数据添加到客户端输出缓冲区。如果函数返回 -1，则不
* 数据应附加到输出缓冲区。 */
int prepareClientToWrite(redis_client_t *c) {
    /* 如果是 Lua 客户端，我们始终返回 ok，而不安装任何
        * 处理程序，因为根本没有套接字。 */
    if (c->flags & (CLIENT_LUA|CLIENT_MODULE)) return 0;

    /* 如果设置了 CLIENT_CLOSE_ASAP 标志，我们就不需要写任何东西。 */
    if (c->flags & CLIENT_CLOSE_ASAP) return -1;

    /* CLIENT REPLY OFF / SKIP 处理：不发送回复。
        * CLIENT_PUSHING 处理：禁用回复静音标志。 */
    if ((c->flags & (CLIENT_REPLY_OFF|CLIENT_REPLY_SKIP)) &&
        !(c->flags & CLIENT_PUSHING)) return -1;

    /* 除非设置了 CLIENT_MASTER_FORCE_REPLY 标志，否则主机不会接收回复。*/
    if ((c->flags & CLIENT_MASTER) &&
        !(c->flags & CLIENT_MASTER_FORCE_REPLY)) return -1;

    if (!c->client.conn) return -1; /* Fake client for AOF loading. */

    /* 安排客户端将输出缓冲区写入套接字，除非
    * 它应该已经设置为这样做（它已经有待处理的数据）。
    *
    * 如果设置了 CLIENT_PENDING_READ，则我们处于 IO 线程中，并且不应安装写入处理程序。相反，它将在返回时由
    * handleClientsWithPendingReadsUsingThreads() 完成。
    */
    if (!clientHasPendingReplies(c) 
        && !(c->flags & CLIENT_PENDING_READ )
       )
            clientInstallWriteHandler(c);

    /* 授权调用者在此客户端的输出缓冲区中排队。 */
    return 0;
}

#define PROTO_REPLY_CHUNK_BYTES (16*1024)

/* Adds the reply to the reply linked list.
 * Note: some edits to this function need to be relayed to AddReplyFromClient. */
void _addReplyProtoToList(redis_client_t *c, const char *s, size_t len) {
    if (c->flags & CLIENT_CLOSE_AFTER_REPLY) return;

    list_node_t*ln = list_last(c->reply);
    client_reply_block_t *tail = ln? list_node_value(ln): NULL;

    /* Note that 'tail' may be NULL even if we have a tail node, because when
     * addReplyDeferredLen() is used, it sets a dummy node to NULL just
     * fo fill it later, when the size of the bulk length is set. */

    /* Append to tail string when possible. */
    if (tail) {
        /* Copy the part we can fit into the tail, and leave the rest for a
         * new node */
        size_t avail = tail->size - tail->used;
        size_t copy = avail >= len? len: avail;
        memcpy(tail->buf + tail->used, s, copy);
        tail->used += copy;
        s += copy;
        len -= copy;
    }
    if (len) {
        /* Create a new node, make sure it is allocated to at
         * least PROTO_REPLY_CHUNK_BYTES */
        size_t size = len < PROTO_REPLY_CHUNK_BYTES? PROTO_REPLY_CHUNK_BYTES: len;
        tail = zmalloc(size + sizeof(client_reply_block_t));
        /* take over the allocation's internal fragmentation */
        tail->size = zmalloc_usable_size(tail) - sizeof(client_reply_block_t);
        tail->used = len;
        memcpy(tail->buf, s, len);
        list_add_node_tail(c->reply, tail);
        c->reply_bytes += tail->size;
        
        //取消客户端限制 
        // closeClientOnOutputBufferLimitReached(c, 1);
    }
}

void __addReply(redis_client_t* c, const char *s, size_t len) {
    if (_addReplyToBuffer(c,s,len) != 0)
        _addReplyProtoToList(c,s,len);
}

void addReply(redis_client_t* c, latte_object_t* obj) {
    if (prepareClientToWrite(c) != 0) return;

    if (sds_encoded_object(obj)) {
        __addReply(c, obj->ptr, sds_len(obj->ptr));
    } else if (obj->encoding == OBJ_ENCODING_INT) {
        char buf[32];
        size_t len = ll2string(buf, sizeof(buf), (long)obj->ptr);
        __addReply(c, buf, len);
    } else {
        latte_panic("Wrong obj->encoding in addReply()");
    }
}

// /* 如果指定客户端有待处理的回复缓冲区要写入套接字，则返回 true。*/
// int clientHasPendingReplies(redis_client_t *c) {
//     return c->bufpos || list_length(c->reply);
// }




/* 此低级函数只是将您发送的任何协议添加到
* 客户端缓冲区，最初尝试静态缓冲区，如果不可能，则使用对象的字符串。
*
* 它很高效，因为如果不需要，则不会创建 SDS 对象或 Redis 对象。如果我们无法扩展对象列表中的现有尾部对象，则仅通过调用
* _addReplyProtoToList() 来创建对象。 */
void addReplyProto(redis_client_t *c, const char *s, size_t len) {
    if (prepareClientToWrite(c) != 0) return;
    __addReply(c, s, len);
}


/* Low level function called by the addReplyError...() functions.
 * It emits the protocol for a Redis error, in the form:
 *
 * -ERRORCODE Error Message<CR><LF>
 *
 * If the error code is already passed in the string 's', the error
 * code provided is used, otherwise the string "-ERR " for the generic
 * error code is automatically added.
 * Note that 's' must NOT end with \r\n. */
void addReplyErrorLength(redis_client_t *c, const char *s, size_t len) {
    /* If the string already starts with "-..." then the error code
     * is provided by the caller. Otherwise we use "-ERR". */
    if (!len || s[0] != '-') addReplyProto(c,"-ERR ",5);
    addReplyProto(c,s,len);
    addReplyProto(c,"\r\n",2);
}

/* See addReplyErrorLength for expectations from the formatted string.
 * The formatted string is safe to contain \r and \n anywhere. */
void addReplyErrorFormat(redis_client_t *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sds_cat_vprintf(sds_empty(),fmt,ap);
    va_end(ap);
    /* Trim any newlines at the end (ones will be added by addReplyErrorLength) */
    s = sds_trim(s, "\r\n");
    /* Make sure there are no newlines in the middle of the string, otherwise
     * invalid protocol is emitted. */
    s = sds_map_chars(s, "\r\n", "  ",  2);
    addReplyErrorLength(c,s,sds_len(s));
    // afterErrorReply(c,s,sds_len(s));
    sds_delete(s);
}

/* See addReplyErrorLength for expectations from the input string. */
void addReplyError(latte_client_t *c, const char *err) {
    addReplyErrorLength(c,err,strlen(err));
    // afterErrorReply(c,err,strlen(err),0);
}




/* Add the SDS 's' string to the client output buffer, as a side effect
 * the SDS string is freed. */
void addReplySds(redis_client_t *c, sds s) {
    if (prepareClientToWrite(c) != 0) {
        /* The caller expects the sds to be free'd. */
        sds_delete(s);
        return;
    }
    __addReply(c, s, sds_len(s));
    sds_delete(s);
}