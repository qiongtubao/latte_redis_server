
#include "client.h"
#include "list/list.h"
#include <string.h>
#include "debug/latte_debug.h"
#include "server.h"
#include "utils/utils.h"
#include "object/string.h"

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
    return c->reply_bytes + (list_item_size*list_length(c->reply));
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