#include "client.h"
#include "server.h"
#include "debug/latte_debug.h"
#include <string.h>
#include "object/string.h"
#include "utils/utils.h"
/* Get the class of a client, used in order to enforce limits to different
 * classes of clients.
 *
 * The function will return one of the following:
 * CLIENT_TYPE_NORMAL -> Normal client
 * CLIENT_TYPE_SLAVE  -> Slave
 * CLIENT_TYPE_PUBSUB -> Client subscribed to Pub/Sub channels
 * CLIENT_TYPE_MASTER -> The client representing our replication master.
 */
int get_client_type(redis_client_t *c) {
    if (c->flag & CLIENT_MASTER) return CLIENT_TYPE_MASTER;
    /* Even though MONITOR clients are marked as replicas, we
     * want the expose them as normal clients. */
    if ((c->flag & CLIENT_SLAVE) && !(c->flag & CLIENT_MONITOR))
        return CLIENT_TYPE_SLAVE;
    if (c->flag & CLIENT_PUBSUB) return CLIENT_TYPE_PUBSUB;
    return CLIENT_TYPE_NORMAL;
}

void call(redis_client_t* rc, int flags) {

    rc->cmd->proc(rc);
    
}

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
    /* Now lookup the command and check ASAP about trivial error conditions
     * such as wrong arity, bad command name and so forth. */
    rc->cmd = rc->lastcmd = command_manager_lookup(server->command_manager, rc->argv[0]->ptr);
    if (rc->cmd == NULL) {
        return -1;
    } 
    call(rc, CMD_CALL_FULL);
    return 0;
}


static void free_client_argv(redis_client_t* rc) {
    int j;
    for (j = 0; j < rc->argc; j++) 
        latte_object_decr_ref_count(rc->argv[j]);
    rc->argc = 0;
    rc->cmd = NULL;
    rc->argv_len_sum = 0;
}

void reset_redis_client(redis_client_t* rc) {
    redis_command_proc_func*  prev_cmd = rc->cmd ?  rc->cmd->proc : NULL;
    free_client_argv(rc);
    rc->req_type = 0;
    rc->multi_bulk_len = 0;
    rc->bulk_len = -1;
    // if (rc->swap_ctx) {
        // swap_ctx_delete(rc->swap_ctx);
    // }
    
    // if (!(rc->flag & CLIENT_MULTI) && prev_cmd != askingCommand) 
    //      c->flags &= ~CLIENT_ASKING;
    
    // if (!(rc->flag & CLIENT_MULTI) && prev_cmd != clientCommand) 
    //      c->flags &= ~CLIENT_TRACKING_CACHING;

    /* Remove the CLIENT_REPLY_SKIP flag if any so that the reply
     * to the next command will be sent, but set the flag if the command
     * we just processed was "CLIENT REPLY SKIP". */
    
    // c->flags &= ~CLIENT_REPLY_SKIP;
    // if (c->flags & CLIENT_REPLY_SKIP_NEXT) {
    //     c->flags |= CLIENT_REPLY_SKIP;
    //     c->flags &= ~CLIENT_REPLY_SKIP_NEXT;
    // }

}

void command_processed(redis_client_t* rc) {
    reset_redis_client(rc);
}

int process_command_and_reset_client(redis_client_t* rc) {
    int deadclient = 0;
    redis_server_t* server = (redis_server_t*)rc->client.server;
    redis_client_t *old_client = server->current_client;
    server->current_client = rc;
    if (process_command(rc) == 0) {
        command_processed(rc);
    }
    if (server->current_client == NULL) deadclient = 1;
    /*
     * Restore the old client, this is needed because when a script
     * times out, we will get into this code from processEventsWhileBlocked.
     * Which will cause to set the server.current_client. If not restored
     * we will return 1 to our caller which will falsely indicate the client
     * is dead and will stop reading from its buffer.
     */
    server->current_client = old_client;
    /* performEvictions may flush slave output buffers. This may
     * result in a slave, that may be the active client, to be
     * freed. */
    return deadclient ? -1 : 0;
}

void set_protocol_error(const char* errstr, redis_client_t* rc) {

}

int process_inline_buffer(redis_client_t* rc){
    char *newline;
    int argc, j, linefeed_chars;
    sds *argv, aux;
    size_t query_len;

    redis_server_t* server = (redis_server_t*)rc->client.server;
    /* Search for end of line */
    newline = strchr(rc->client.querybuf + rc->client.qb_pos, '\n');

    /* Nothing to do without a \r\n */
    if (newline == NULL) {
        if (sds_len(rc->client.querybuf)-rc->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
            add_reply_error(rc,"Protocol error: too big inline request");
            set_protocol_error("too big inline request",rc);
        }
        return -1;
    }
    /* Handle the \r\n case. */
    if (newline != rc->client.querybuf+rc->client.qb_pos && *(newline-1) == '\r')
        newline--, linefeed_chars++;
    /* Split the input buffer up to the \r\n */
    query_len = newline-(rc->client.querybuf+rc->client.qb_pos);
    aux = sds_new_len(rc->client.querybuf+rc->client.qb_pos,query_len);
    argv = sds_split_args(aux,&argc);
    sds_delete(aux);
    if (argv == NULL) {
        add_reply_error(rc, "Protocol error: unbalanced quotes in request");
        set_protocol_error("unbalanced quotes in inline request",rc);
        return -1;
    }
    /* Newline from slaves can be used to refresh the last ACK time.
     * This is useful for a slave to ping back while loading a big
     * RDB file. */
    if (query_len == 0 && get_client_type(rc) == CLIENT_TYPE_SLAVE)
        rc->repl_ack_time = server->unixtime;
    /* Masters should never send us inline protocol to run actual
     * commands. If this happens, it is likely due to a bug in Redis where
     * we got some desynchronization in the protocol, for example
     * beause of a PSYNC gone bad.
     *
     * However the is an exception: masters may send us just a newline
     * to keep the connection active. */
    if (query_len != 0 && rc->flag & CLIENT_MASTER) {
        sds_free_splitres(argv,argc);
        LATTE_LIB_LOG(LL_WARN,"WARNING: Receiving inline protocol from master, master stream corruption? Closing the master connection and discarding the cached master.");
        set_protocol_error("Master using the inline protocol. Desync?",rc);
        return -1;
    }

    /* Move querybuffer position to the next query in the buffer. */
    rc->client.qb_pos += query_len + linefeed_chars;

    /* Setup argv array on client structure */
    if (argc) {
        if (rc->argv) zfree(rc->argv);
        rc->argv = zmalloc(sizeof(latte_object_t*)*argc);
        rc->argv_len_sum = 0;
    }
    /* Create redis objects for all arguments. */
    for (rc->argc = 0, j = 0; j < argc; j++) {
        rc->argv[rc->argc] = latte_object_new(OBJ_STRING,argv[j]);
        rc->argc++;
        rc->argv_len_sum += sds_len(argv[j]);
    }
    zfree(argv);
    return 0;
}

int auth_required(redis_client_t *c) {
    /* Check if the user is authenticated. This check is skipped in case
     * the default user is flagged as "nopass" and is active. */
    // int auth_required = (!(DefaultUser->flags & USER_FLAG_NOPASS) ||
    //                       (DefaultUser->flags & USER_FLAG_DISABLED)) &&
    //                     !c->authenticated;
    // return auth_required;
    return 0;
}
int process_multibulk_buffer(redis_client_t* rc) {
    char* newline = NULL;
    int ok;
    long long ll;

    redis_server_t* server = (redis_server_t*)rc->client.server;
    if (rc->multi_bulk_len == 0) {
        /* The client should have been reset */
        latte_assert_with_info(rc->argc == 0, "client argc ! = 0");
        /* Multi bulk length cannot be read without a \r\n */
        newline = strchr(rc->client.querybuf+rc->client.qb_pos,'\r');
        if (newline == NULL) {
            if (sds_len(rc->client.querybuf)-rc->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
                add_reply_error(rc,"Protocol error: too big mbulk count string");
                set_protocol_error("too big mbulk count string",rc);
            }
            return -1;
        }

        /* Buffer should also contain \n */
        if (newline-(rc->client.querybuf+rc->client.qb_pos) > (ssize_t)(sds_len(rc->client.querybuf)-rc->client.qb_pos-2))
            return -1;

        /* We know for sure there is a whole line since newline != NULL,
         * so go ahead and find out the multi bulk length. */
        latte_assert_with_info(rc->client.querybuf[rc->client.qb_pos] == '*', "querybuf[0] != '*'");
        ok = string2ll(rc->client.querybuf + 1 + rc->client.qb_pos, newline - (rc->client.querybuf+1+rc->client.qb_pos),&ll);
        if (!ok || ll > 1024*1024) {
            add_reply_error(rc,"Protocol error: invalid multibulk length");
            set_protocol_error("invalid mbulk count",rc);
            return -1;
        } else if (ll > 10 && auth_required(rc)) {
            add_reply_error(rc, "Protocol error: unauthenticated multibulk length");
            set_protocol_error("unauth mbulk count", rc);
            return -1;
        }

        rc->client.qb_pos = (newline-rc->client.querybuf) + 2;

        if (ll <= 0) return 0;

        rc->multi_bulk_len = ll;

        if (rc->argv) zfree(rc->argv);
        rc->argv = zmalloc(sizeof(latte_object_t*)*rc->multi_bulk_len);
        rc->argv_len_sum = 0;
    }

    latte_assert_with_info(rc->multi_bulk_len > 0, "rc->multibulklen = %d ", rc->multi_bulk_len);
    while(rc->multi_bulk_len) {
        /* Read bulk length if unknown */
        if (rc->bulk_len == -1) {
            newline = strchr(rc->client.querybuf+rc->client.qb_pos,'\r');
            if (newline == NULL) {
                if (sds_len(rc->client.querybuf)-rc->client.qb_pos > PROTO_INLINE_MAX_SIZE) {
                    add_reply_error(rc,
                        "Protocol error: too big bulk count string");
                    set_protocol_error("too big bulk count string",rc);
                    return -1;
                }
                break;
            }

            /* Buffer should also contain \n */
            if (newline-(rc->client.querybuf+rc->client.qb_pos) > (ssize_t)(sds_len(rc->client.querybuf)-rc->client.qb_pos-2))
                break;

            if (rc->client.querybuf[rc->client.qb_pos] != '$') {
                add_reply_error_format(rc,
                    "Protocol error: expected '$', got '%c'",
                    rc->client.querybuf[rc->client.qb_pos]);
                set_protocol_error("expected $ but got something else",rc);
                return -1;
            }

            ok = string2ll(rc->client.querybuf+rc->client.qb_pos+1,newline-(rc->client.querybuf+rc->client.qb_pos+1),&ll);
            if (!ok || ll < 0 ||
                (!(rc->flag & CLIENT_MASTER) && ll > 
                server->proto_max_bulk_len)) {
                add_reply_error(rc,"Protocol error: invalid bulk length");
                set_protocol_error("invalid bulk length",rc);
                return -1;
            } else if (ll > 16384 && auth_required(rc)) {
                add_reply_error(rc, "Protocol error: unauthenticated bulk length");
                set_protocol_error("unauth bulk length", rc);
                return -1;
            }

            rc->client.qb_pos = newline-rc->client.querybuf+2;
            if (ll >= PROTO_MBULK_BIG_ARG) {
                /* If we are going to read a large object from network
                 * try to make it likely that it will start at c->querybuf
                 * boundary so that we can optimize object creation
                 * avoiding a large copy of data.
                 *
                 * But only when the data we have not parsed is less than
                 * or equal to ll+2. If the data length is greater than
                 * ll+2, trimming querybuf is just a waste of time, because
                 * at this time the querybuf contains not only our bulk. */
                if (sds_len(rc->client.querybuf)-rc->client.qb_pos <= (size_t)ll+2) {
                    sds_range(rc->client.querybuf,rc->client.qb_pos,-1);
                    rc->client.qb_pos = 0;
                    /* Hint the sds library about the amount of bytes this string is
                     * going to contain. */
                    rc->client.querybuf = sds_make_room_for(rc->client.querybuf,ll+2-sds_len(rc->client.querybuf));
                }
            }
            rc->bulk_len = ll;
        }

        /* Read bulk argument */
        if (sds_len(rc->client.querybuf)-rc->client.qb_pos < (size_t)(rc->bulk_len+2)) {
            /* Not enough data (+2 == trailing \r\n) */
            break;
        } else {
            /* Optimization: if the buffer contains JUST our bulk element
             * instead of creating a new object by *copying* the sds we
             * just use the current sds string. */
            if (rc->client.qb_pos == 0 &&
                rc->bulk_len >= PROTO_MBULK_BIG_ARG &&
                sds_len(rc->client.querybuf) == (size_t)(rc->bulk_len+2))
            {
                rc->argv[rc->argc++] = latte_object_new(OBJ_STRING,rc->client.querybuf);
                rc->argv_len_sum += rc->bulk_len;
                sds_incr_len(rc->client.querybuf,-2); /* remove CRLF */
                /* Assume that if we saw a fat argument we'll see another one
                 * likely... */
                rc->client.querybuf = sds_new_len(SDS_NOINIT,rc->bulk_len+2);
                sds_clear(rc->client.querybuf);
            } else {
                rc->argv[rc->argc++] =
                    string_object_new(rc->client.querybuf+rc->client.qb_pos,rc->bulk_len);
                rc->argv_len_sum += rc->bulk_len;
                rc->client.qb_pos += rc->bulk_len+2;
            }
            rc->bulk_len = -1;
            rc->multi_bulk_len--;
        }
    }

    /* We're done when c->multibulk == 0 */
    if (rc->multi_bulk_len == 0) return 0;

    /* Still not ready to process the command */
    return -1;
}





int redis_process_input_buffer(redis_client_t* rc) {
    //解析读取的数据 转换成object 对象
    /* Keep processing while there is something in the input buffer */
    while (rc->client.qb_pos < sds_len(rc->client.querybuf)) {
        /* Immediately abort if the client is in the middle of something. */
        //if (rc->flag & CLIENT_BLOCKED break;
        
        /* Also abort if the client is swapping. */
        //if (rc->flags & CLIENT_SWAPPING || c->flags & CLIENT_SWAP_REWINDING) break;

        /* Don't process more buffers from clients that have already pending
         * commands to execute in c->argv. */
        // if (c->flags & CLIENT_PENDING_COMMAND) break;

        /* Don't process input from the master while there is a busy script
         * condition on the slave. We want just to accumulate the replication
         * stream (instead of replying -BUSY like we do with other clients) and
         * later resume the processing. */
        //if (server.lua_timedout && rc->flags & CLIENT_MASTER) break;
        
        /* CLIENT_CLOSE_AFTER_REPLY closes the connection once the reply is
         * written to the client. Make sure to not let the reply grow after
         * this flag has been set (i.e. don't process more commands).
         *
         * The same applies for clients we want to terminate ASAP. */
        if (rc->client.flags & (CLIENT_CLOSE_AFTER_REPLY|CLIENT_CLOSE_ASAP)) break;
        
        if (!rc->req_type) {
            if (rc->client.querybuf[rc->client.qb_pos] == '*') { //判定协议如果开头是* 就是redis协议
                rc->req_type = PROTO_REQ_MULTIBULK;
            } else {
                rc->req_type = PROTO_REQ_INLINE;
            }
        }
        
        if (rc->req_type == PROTO_REQ_INLINE) { //普通协议
            if (process_inline_buffer(rc) != 0) break;
            /* If the Gopher mode and we got zero or one argument, process
             * the request in Gopher mode. To avoid data race, Redis won't
             * support Gopher if enable io threads to read queries. */
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
            redis_panic("Unknown request type");
        }

        /* Multibulk processing could see a <= 0 length. */
        if (rc->argc == 0) {
            reset_redis_client(rc);
        } else {
            /* If we are in the context of an I/O thread, we can't really
             * execute the command here. All we can do is to flag the client
             * as one that needs to process the command. */
            // if (rc->flags & CLIENT_PENDING_READ) {
            //     rc->flags |= CLIENT_PENDING_COMMAND;
            //     break;
            // }
        
            /* We are finally ready to execute the command. */
            if (process_command_and_reset_client(rc) == -1) {
                /* If the client is no longer valid, we avoid exiting this
                 * loop and trimming the client buffer later. So we return
                 * ASAP in that case. */
                return 0;
            }
        }
    }

    if (rc->client.qb_pos) return 1;
    return 0;
}
int redis_client_handle(struct latte_client_t* lc, int nread) {
    redis_client_t* rc = (redis_client_t*)lc;
    redis_server_t* server = (redis_server_t*)lc->server;
    //rc->lastinteraction = lc->server.unixtime;
    if (rc->flag & CLIENT_MASTER) {
        rc->read_reploff += nread;
    }
    // latte_atomic_incr(server.stat_net_input_bytes, nread); //统计读取数据
    // if (sds_len(lc->querybuf) > server.client_max_querybuf_len) { //如果读取数据大于每次请求次数的时候 返回错误断开客户端
    //     sds ci = catClientInfoString(sdsempty(),c), bytes = sdsempty();

    //     bytes = sdscatrepr(bytes,c->querybuf,64);
    //     serverLog(LL_WARNING,"Closing client that reached max query buffer length: %s (qbuf initial bytes: %s)", ci, bytes);
    //     sdsfree(ci);
    //     sdsfree(bytes);
    //     freeClientAsync(c);
    //     return;
    // }
    /* There is more data in the client input buffer, continue parsing it
     * in case to check if there is a full command to execute. */
    return redis_process_input_buffer(rc);
}

latte_client_t* create_redis_client() {
    redis_client_t* redis_client = zmalloc(sizeof(redis_client_t));
    redis_client->client.exec = redis_client_handle;
    redis_client->dbid = 0;
    redis_client->multi_bulk_len = 0;
    redis_client->bulk_len = -1; //非常重要 ！ ！ ！ redis协议命令解析用
    return (latte_client_t*)redis_client;
}
void redis_client_delete(latte_client_t* client) {
    redis_client_t* redis_client = (redis_client_t*)client;
    zfree(redis_client);
}


void free_redis_client(redis_client_t *rc) {
    list_node_t *ln;

    redis_server_t* server = (redis_server_t*)rc->client.server;
    /* Unlinked repl client from server.repl_swapping_clients. */
    // repl_client_discard_swapping_state(c);

    /* If a client is protected, yet we need to free it right now, make sure
     * to at least use asynchronous freeing. */
    // if (c->flags & CLIENT_PROTECTED || c->flags & CLIENT_SWAP_UNLOCKING) {
    //     free_redis_client_async(c);
    //     return;
    // }

    /* For connected clients, call the disconnection event of modules hooks. */
    // if (c->conn) {
    //     moduleFireServerEvent(REDISMODULE_EVENT_CLIENT_CHANGE,
    //                           REDISMODULE_SUBEVENT_CLIENT_CHANGE_DISCONNECTED,
    //                           c);
    // }

    /* Notify module system that this client auth status changed. */
    // moduleNotifyUserChanged(c);

    /* If this client was scheduled for async freeing we need to remove it
     * from the queue. Note that we need to do this here, because later
     * we may call replicationCacheMaster() and the client should already
     * be removed from the list of clients to free. */
    if (rc->client.flags & CLIENT_CLOSE_ASAP) {
        ln = list_search_key(server->clients_to_close,rc);
        latte_assert(ln != NULL);
        list_del_node(server->clients_to_close,ln);
    }

    
}

/* Schedule a client to free it at a safe time in the serverCron() function.
    * This function is useful when we need to terminate a client but we are in
    * a context where calling freeClient() is not possible, because the client
    * should be valid for the continuation of the flow of the program. */
void free_redis_client_async(redis_client_t* rc) {
    /* We need to handle concurrent access to the server.clients_to_close list
     * only in the freeClientAsync() function, since it's the only function that
     * may access the list while Redis uses I/O threads. All the other accesses
     * are in the context of the main thread while the other threads are
     * idle. */
    if (rc->client.flags & CLIENT_CLOSE_ASAP || rc->flag & CLIENT_LUA) return;
    rc->client.flags |= CLIENT_CLOSE_ASAP;
    redis_server_t* server = (redis_server_t*)rc->client.server;

    { //暂时默认io_threads_num 为1  
        list_add_node_tail(server->clients_to_close,rc);
        return;
    }
    // if (server.io_threads_num == 1) {
    //     /* no need to bother with locking if there's just one thread (the main thread) */
    //     list_add_node_tail(server.clients_to_close,rc);
    //     return;
    // }
    //为什么别的地方不需要加锁（比如删除掉客户端）  只有这里追加到最后的客户端需要加锁
    // static pthread_mutex_t async_free_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
    // pthread_mutex_lock(&async_free_queue_mutex);
    // list_add_node_tail(server->clients_to_close, rc);
    // pthread_mutex_unlock(&async_free_queue_mutex);
}

