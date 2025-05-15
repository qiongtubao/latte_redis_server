
#include "client.h"
#include "list/list.h"
#include <string.h>
#include "debug/latte_debug.h"
#include "server.h"
#include "utils/utils.h"
#include "object/string.h"
int prepare_client_to_write(redis_client_t* c) {
    return 0;
}


sds cat_redis_client_info_string(sds s, redis_client_t *client) {
    return s;
}


/* This function returns the number of bytes that Redis is
 * using to store the reply still not read by the client.
 *
 * Note: this function is very fast so can be called as many time as
 * the caller wishes. The main usage of this function currently is
 * enforcing the client output length limits. */
unsigned long get_client_output_buffer_memory_usage(redis_client_t *c) {
    unsigned long list_item_size = sizeof(list_node_t) + sizeof(client_reply_block_t);
    return c->reply_bytes + (list_item_size*list_length(c->client.reply));
}
/* The function checks if the client reached output buffer soft or hard
 * limit, and also update the state needed to check the soft limit as
 * a side effect.
 *
 * Return value: non-zero if the client reached the soft or the hard limit.
 *               Otherwise zero is returned. */
int check_client_output_buffer_limits(redis_client_t *c) {
    return 0;
    // int soft = 0, hard = 0, class;
    // unsigned long used_mem = get_client_output_buffer_memory_usage(c);

    // class = get_client_type(c);
    // /* For the purpose of output buffer limiting, masters are handled
    //  * like normal clients. */
    // if (class == CLIENT_TYPE_MASTER) class = CLIENT_TYPE_NORMAL;

    // redis_server_t* server = c->client.server;

    // if (server->client_obuf_limits[class].hard_limit_bytes &&
    //     used_mem >= server.client_obuf_limits[class].hard_limit_bytes)
    //     hard = 1;
    // if (server->client_obuf_limits[class].soft_limit_bytes &&
    //     used_mem >= server.client_obuf_limits[class].soft_limit_bytes)
    //     soft = 1;

    // /* We need to check if the soft limit is reached continuously for the
    //  * specified amount of seconds. */
    // if (soft) {
    //     if (c->obuf_soft_limit_reached_time == 0) {
    //         c->obuf_soft_limit_reached_time = server.unixtime;
    //         soft = 0; /* First time we see the soft limit reached */
    //     } else {
    //         time_t elapsed = server.unixtime - c->obuf_soft_limit_reached_time;

    //         if (elapsed <=
    //             server.client_obuf_limits[class].soft_limit_seconds) {
    //             soft = 0; /* The client still did not reached the max number of
    //                          seconds for the soft limit to be considered
    //                          reached. */
    //         }
    //     }
    // } else {
    //     c->obuf_soft_limit_reached_time = 0;
    // }
    // return soft || hard;
}




/* Asynchronously close a client if soft or hard limit is reached on the
 * output buffer size. The caller can check if the client will be closed
 * checking if the client CLIENT_CLOSE_ASAP flag is set.
 *
 * Note: we need to close the client asynchronously because this function is
 * called from contexts where the client can't be freed safely, i.e. from the
 * lower level functions pushing data inside the client output buffers.
 * When `async` is set to 0, we close the client immediately, this is
 * useful when called from cron.
 *
 * Returns 1 if client was (flagged) closed. */
int close_client_on_output_buffer_limit_reached(redis_client_t *c, int async) {
    if (!c->client.conn) return 0; /* It is unsafe to free fake clients. */
    latte_assert(c->reply_bytes < SIZE_MAX-(1024*64));
    if (c->reply_bytes == 0 || c->client.flags & CLIENT_CLOSE_ASAP) return 0;
    if (check_client_output_buffer_limits(c)) {
        sds client = cat_redis_client_info_string(sds_empty(),c);

        if (async) {
            free_redis_client_async(c);
            LATTE_LIB_LOG(LL_WARN,
                      "Client %s scheduled to be closed ASAP for overcoming of output buffer limits.",
                      client);
        } else {
            free_redis_client(c);
            LATTE_LIB_LOG(LL_WARN,
                      "Client %s closed for overcoming of output buffer limits.",
                      client);
        }
        sds_delete(client);
        return  1;
    }
    return 0;
}

/* See addReplyErrorLength for expectations from the input string. */
void add_reply_error(redis_client_t *c, const char *err) {
    add_reply_error_length(c, err, strlen(err));
    // after_error_reply(c,err,strlen(err));
}

void add_reply_error_length(redis_client_t* c, const char *s, size_t len) {
    /* If the string already starts with "-..." then the error code
     * is provided by the caller. Otherwise we use "-ERR". */
    if (!len || s[0] != '-') add_reply_proto(c,"-ERR ",5);
    add_reply_proto(c,s,len);
    add_reply_proto(c,"\r\n",2);
}



/* See addReplyErrorLength for expectations from the formatted string.
 * The formatted string is safe to contain \r and \n anywhere. */
void add_reply_error_format(redis_client_t *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sds_cat_vprintf(sds_empty(),fmt,ap);
    va_end(ap);
    /* Trim any newlines at the end (ones will be added by addReplyErrorLength) */
    s = sds_trim(s, "\r\n");
    /* Make sure there are no newlines in the middle of the string, otherwise
     * invalid protocol is emitted. */
    s = sds_map_chars(s, "\r\n", "  ",  2);
    add_reply_error_length(c,s,sds_len(s));
    // after_error_reply(c,s,sds_len(s));
    sds_delete(s);
}

void add_reply(redis_client_t* c, latte_object_t* obj) {
    if (prepare_client_to_write(c) != 0) return;
    if (sds_encoded_object(obj)) {
        add_reply_proto(c, obj->ptr,sds_len(obj->ptr));
    } else if (obj->encoding == OBJ_ENCODING_INT) {
        /* For integer encoded strings we just convert it into a string
         * using our optimized function, and attach the resulting string
         * to the output buffer. */
        char buf[32];
        size_t len = ll2string(buf,sizeof(buf),(long)obj->ptr);
         add_reply_proto(c, buf, len);
    } else {
        redis_panic("Wrong obj->encoding in addReply()");
    }
}

/* Add a long long as integer reply or bulk len / multi bulk count.
 * Basically this is used to output <prefix><long long><crlf>. */
void add_reply_long_long_with_prefix(redis_client_t *c, long long ll, char prefix) {
    char buf[128];
    int len;

    /* Things like $3\r\n or *2\r\n are emitted very often by the protocol
     * so we have a few shared objects to use if the integer is small
     * like it is most of the times. */
    if (prefix == '*' && ll < OBJ_SHARED_BULKHDR_LEN && ll >= 0) {
        add_reply(c,shared.mbulkhdr[ll]);
        return;
    } else if (prefix == '$' && ll < OBJ_SHARED_BULKHDR_LEN && ll >= 0) {
        add_reply(c,shared.bulkhdr[ll]);
        return;
    }

    buf[0] = prefix;
    len = ll2string(buf+1,sizeof(buf)-1,ll);
    buf[len+1] = '\r';
    buf[len+2] = '\n';
    add_reply_proto(c,buf,len+3);
}


/* Create the length prefix of a bulk reply, example: $2234 */
void add_reply_bulk_len(redis_client_t *c, latte_object_t *obj) {
    size_t len = string_object_len(obj);

    add_reply_long_long_with_prefix(c,len,'$');
}

void add_reply_bulk(redis_client_t* c, latte_object_t* obj) {
    add_reply_bulk_len(c,obj);
    add_reply(c,obj);
    add_reply(c,shared.crlf);
}


void trim_reply_unused_tail_space(redis_client_t* c) {
    list_node_t* ln = list_last(c->client.reply);

    client_reply_block_t* tail = ln? list_node_value(ln): NULL;

    if (!tail) return;

    if (tail->size - tail->used > tail->size / 4 && 
        tail->used < PROTO_REPLY_CHUNK_BYTES) {
            size_t old_size = tail->size;
            tail = zrealloc(tail, tail->used + sizeof(client_reply_block_t));

        tail->size = zmalloc_usable_size(tail) - sizeof(client_reply_block_t);
        c->reply_bytes = c->reply_bytes + tail->size - old_size;
        list_node_value(ln) = tail;
    }
}

void* add_reply_deferred_len(redis_client_t* c) {
    if (prepare_client_to_write(c) != 0) return NULL;
    
    trim_reply_unused_tail_space(c);
    list_add_node_tail(c->client.reply, NULL);
    return list_last(c->client.reply);
}

void add_reply_status_length(redis_client_t *c, const char *s, size_t len) {
    add_reply_proto(c,"+",1);
    add_reply_proto(c,s,len);
    add_reply_proto(c,"\r\n",2);
}

void add_reply_status_format(redis_client_t *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sds_cat_vprintf(sds_empty(),fmt,ap);
    va_end(ap);
    add_reply_status_length(c,s,sds_len(s));
    sds_delete(s);
}

void add_reply_status(redis_client_t *c, const char *status) {
    add_reply_status_length(c,status,strlen(status));
}

void set_deferred_reply(redis_client_t *c, void *node, const char *s, size_t length) {
    list_node_t *ln = (list_node_t*)node;
    client_reply_block_t *next, *prev;

    /* Abort when *node is NULL: when the client should not accept writes
     * we return NULL in addReplyDeferredLen() */
    if (node == NULL) return;
    latte_assert(!list_node_value(ln));

    /* Normally we fill this dummy NULL node, added by addReplyDeferredLen(),
     * with a new buffer structure containing the protocol needed to specify
     * the length of the array following. However sometimes there might be room
     * in the previous/next node so we can instead remove this NULL node, and
     * suffix/prefix our data in the node immediately before/after it, in order
     * to save a write(2) syscall later. Conditions needed to do it:
     *
     * - The prev node is non-NULL and has space in it or
     * - The next node is non-NULL,
     * - It has enough room already allocated
     * - And not too large (avoid large memmove) */
    if (ln->prev != NULL && (prev = list_node_value(ln->prev)) &&
        prev->size - prev->used > 0)
    {
        size_t len_to_copy = prev->size - prev->used;
        if (len_to_copy > length)
            len_to_copy = length;
        memcpy(prev->buf + prev->used, s, len_to_copy);
        prev->used += len_to_copy;
        length -= len_to_copy;
        if (length == 0) {
            list_del_node(c->client.reply, ln);
            return;
        }
        s += len_to_copy;
    }

    if (ln->next != NULL && (next = list_node_value(ln->next)) &&
        next->size - next->used >= length &&
        next->used < PROTO_REPLY_CHUNK_BYTES * 4)
    {
        memmove(next->buf + length, next->buf, next->used);
        memcpy(next->buf, s, length);
        next->used += length;
        list_del_node(c->client.reply,ln);
    } else {
        /* Create a new node */
        client_reply_block_t *buf = zmalloc(length + sizeof(client_reply_block_t));
        /* Take over the allocation's internal fragmentation */
        buf->size = zmalloc_usable_size(buf) - sizeof(client_reply_block_t);
        buf->used = length;
        memcpy(buf->buf, s, length);
        list_node_value(ln) = buf;
        c->reply_bytes += buf->size;

        // closeClientOnOutputBufferLimitReached(c, 1);
    }
}

/* Populate the length object and try gluing it to the next chunk. */
void set_deferred_aggregate_len(redis_client_t *c, void *node, long length, char prefix) {
    latte_assert(length >= 0);

    /* Abort when *node is NULL: when the client should not accept writes
     * we return NULL in addReplyDeferredLen() */
    if (node == NULL) return;

    char lenstr[128];
    size_t lenstr_len = sprintf(lenstr, "%c%ld\r\n", prefix, length);
    set_deferred_reply(c, node, lenstr, lenstr_len);
}

void set_deferred_array_len(redis_client_t *c, void *node, long length) {
    set_deferred_aggregate_len(c,node,length,'*');
}

void add_reply_help(redis_client_t* c, char** help) {
    sds cmd = sds_new((char*) c->argv[0]->ptr);

    void *blenp = add_reply_deferred_len(c);
    int blen = 0;

    sds_to_upper(cmd);
    add_reply_status_format(c, "%s <subcommand> [<arg> [value] [opt] ...]. Subcommands are:", cmd);
    sds_delete(cmd);
    while (help[blen]) add_reply_status(c, help[blen++]);

    add_reply_status(c, "HELP");
    add_reply_status(c, "   Prints this help.");

    blen += 1; /* Account for the header. */
    blen += 2; /* Account for the footer. */
    set_deferred_array_len(c, blenp, blen);

}