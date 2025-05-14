
#ifndef __REDIS_CLIENT_H
#define __REDIS_CLIENT_H

#include "server/client.h"
#include "command.h"
#include "list/list.h"
#define PROTO_REQ_INLINE 1
#define PROTO_REQ_MULTIBULK 2
/* Client flags */
#define CLIENT_SLAVE (1<<0)   /* This client is a replica */
#define CLIENT_MASTER (1<<1)  /* This client is a master */
#define CLIENT_MONITOR (1<<2) /* This client is a slave monitor, see MONITOR */
#define CLIENT_MULTI (1<<3)   /* This client is in a MULTI context */
#define CLIENT_BLOCKED (1<<4) /* The client is waiting in a blocking operation */
#define CLIENT_DIRTY_CAS (1<<5) /* Watched keys modified. EXEC will fail. */
// #define CLIENT_CLOSE_AFTER_REPLY (1<<6) /* Close after writing entire reply. */
#define CLIENT_UNBLOCKED (1<<7) /* This client was unblocked and is stored in
                                  server.unblocked_clients */
#define CLIENT_LUA (1<<8) /* This is a non connected client used by Lua */
#define CLIENT_ASKING (1<<9)     /* Client issued the ASKING command */
// #define CLIENT_CLOSE_ASAP (1<<10)/* Close this client ASAP */
#define CLIENT_UNIX_SOCKET (1<<11) /* Client connected via Unix domain socket */
#define CLIENT_DIRTY_EXEC (1<<12)  /* EXEC will fail for errors while queueing */
#define CLIENT_MASTER_FORCE_REPLY (1<<13)  /* Queue replies even if is master */
#define CLIENT_FORCE_AOF (1<<14)   /* Force AOF propagation of current cmd. */
#define CLIENT_FORCE_REPL (1<<15)  /* Force replication of current cmd. */
#define CLIENT_PRE_PSYNC (1<<16)   /* Instance don't understand PSYNC. */
#define CLIENT_READONLY (1<<17)    /* Cluster client is in read-only state. */
#define CLIENT_PUBSUB (1<<18)      /* Client is in Pub/Sub mode. */
#define CLIENT_PREVENT_AOF_PROP (1<<19)  /* Don't propagate to AOF. */
#define CLIENT_PREVENT_REPL_PROP (1<<20)  /* Don't propagate to slaves. */
#define CLIENT_PREVENT_PROP (CLIENT_PREVENT_AOF_PROP|CLIENT_PREVENT_REPL_PROP)
#define CLIENT_PENDING_WRITE (1<<21) /* Client has output to send but a write */
            


/* Client classes for client limits, currently used only for
 * the max-client-output-buffer limit implementation. */
#define CLIENT_TYPE_NORMAL 0 /* Normal req-reply clients + MONITORs */
#define CLIENT_TYPE_SLAVE 1  /* Slaves. */
#define CLIENT_TYPE_PUBSUB 2 /* Clients subscribed to PubSub channels. */
#define CLIENT_TYPE_MASTER 3 /* Master. */
#define CLIENT_TYPE_COUNT 4  /* Total number of client types. */
#define CLIENT_TYPE_OBUF_COUNT 3 /* Number of clients to expose to output */
 

#define PROTO_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */

typedef struct redis_client_t {
    latte_client_t client;
    int flag;
    int req_type;
    long long read_reploff;
    int argc;
    latte_object_t** argv;
    redis_command_t* cmd;
    redis_command_t* lastcmd;
    size_t argv_len_sum;
    long bulk_len;
    int multi_bulk_len;
    list_t* reply;
    unsigned long long reply_bytes;
    sds pending_querybuf;
    long long repl_ack_time;
} redis_client_t;

latte_client_t* create_redis_client();
int get_client_type(redis_client_t *c);
//是否合并redis_client_delete  && free_redis_client
void redis_client_delete(latte_client_t* client);
void free_redis_client(redis_client_t* client);
void free_redis_client_async(redis_client_t* client);

void add_reply(redis_client_t* c, latte_object_t* o);
void add_reply_error(redis_client_t *c, const char *err);
void add_reply_error_length(redis_client_t* rc, const char *s, size_t len);
void add_reply_error_format(redis_client_t *c, const char *fmt, ...);
void add_reply_bulk(redis_client_t* c, latte_object_t* obj);
#endif
