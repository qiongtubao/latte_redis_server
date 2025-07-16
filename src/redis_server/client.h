

#ifndef __LATTE_SERVER_CLIENT_H
#define __LATTE_SERVER_CLIENT_H
#include "server/server.h"
#include "../redis.h"
#include "object/object.h"
#include "list/list.h"
#include "server/client.h"

/* Client flags */
#define CLIENT_SLAVE (1<<0)   /* This client is a replica */
#define CLIENT_MASTER (1<<1)  /* This client is a master */
#define CLIENT_MONITOR (1<<2) /* This client is a slave monitor, see MONITOR */
#define CLIENT_MULTI (1<<3)   /* This client is in a MULTI context */
#define CLIENT_BLOCKED (1<<4) /* The client is waiting in a blocking operation */
#define CLIENT_DIRTY_CAS (1<<5) /* Watched keys modified. EXEC will fail. */
#define CLIENT_CLOSE_AFTER_REPLY (1<<6) /* Close after writing entire reply. */
#define CLIENT_UNBLOCKED (1<<7) /* This client was unblocked and is stored in
                                  server.unblocked_clients */
#define CLIENT_LUA (1<<8) /* This is a non connected client used by Lua */
#define CLIENT_ASKING (1<<9)     /* Client issued the ASKING command */
#define CLIENT_CLOSE_ASAP (1<<10)/* Close this client ASAP */
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
#define CLIENT_PENDING_WRITE (1<<21) /* Client has output to send but a write
                                        handler is yet not installed. */
#define CLIENT_REPLY_OFF (1<<22)   /* Don't send replies to client. */
#define CLIENT_REPLY_SKIP_NEXT (1<<23)  /* Set CLIENT_REPLY_SKIP for next cmd */
#define CLIENT_REPLY_SKIP (1<<24)  /* Don't send just this reply. */
#define CLIENT_LUA_DEBUG (1<<25)  /* Run EVAL in debug mode. */
#define CLIENT_LUA_DEBUG_SYNC (1<<26)  /* EVAL debugging without fork() */
#define CLIENT_MODULE (1<<27) /* Non connected client used by some module. */
#define CLIENT_PROTECTED (1<<28) /* Client should not be freed for now. */
#define CLIENT_PENDING_READ (1<<29) /* The client has pending reads and was put
                                       in the list of clients we can read
                                       from. */
#define CLIENT_PENDING_COMMAND (1<<30) /* Indicates the client has a fully
                                        * parsed command ready for execution. */
#define CLIENT_TRACKING (1ULL<<31) /* Client enabled keys tracking in order to
                                   perform client side caching. */
#define CLIENT_TRACKING_BROKEN_REDIR (1ULL<<32) /* Target client is invalid. */
#define CLIENT_TRACKING_BCAST (1ULL<<33) /* Tracking in BCAST mode. */
#define CLIENT_TRACKING_OPTIN (1ULL<<34)  /* Tracking in opt-in mode. */
#define CLIENT_TRACKING_OPTOUT (1ULL<<35) /* Tracking in opt-out mode. */
#define CLIENT_TRACKING_CACHING (1ULL<<36) /* CACHING yes/no was given,
                                              depending on optin/optout mode. */
#define CLIENT_TRACKING_NOLOOP (1ULL<<37) /* Don't send invalidation messages
                                             about writes performed by myself.*/
#define CLIENT_IN_TO_TABLE (1ULL<<38) /* This client is in the timeout table. */
#define CLIENT_PROTOCOL_ERROR (1ULL<<39) /* Protocol error chatting with it. */
#define CLIENT_CLOSE_AFTER_COMMAND (1ULL<<40) /* Close after executing commands
                                               * and writing entire reply. */
#define CLIENT_DENY_BLOCKING (1ULL<<41) /* Indicate that the client should not be blocked.
                                           currently, turned on inside MULTI, Lua, RM_Call,
                                           and AOF client */
#define CLIENT_REPL_RDBONLY (1ULL<<42) /* This client is a replica that only wants
                                          RDB without replication buffer. */
#define CLIENT_PUSHING (1ULL<<43) /* This client is pushing notifications. */

/* Client classes for client limits, currently used only for
 * the max-client-output-buffer limit implementation. */
#define CLIENT_TYPE_NORMAL 0 /* Normal req-reply clients + MONITORs */
#define CLIENT_TYPE_SLAVE 1  /* Slaves. */
#define CLIENT_TYPE_PUBSUB 2 /* Clients subscribed to PubSub channels. */
#define CLIENT_TYPE_MASTER 3 /* Master. */
#define CLIENT_TYPE_COUNT 4  /* Total number of client types. */
#define CLIENT_TYPE_OBUF_COUNT 3 /* Number of clients to expose to output
                                    buffer configuration. Just the first
                                    three: normal, slave, pubsub. */
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define MAX_KEYS_BUFFER 256
typedef struct get_keys_result_t {
    int keysbuf[MAX_KEYS_BUFFER];       /* Pre-allocated buffer, to save heap allocations */
    int *keys;                          /* Key indices array, points to keysbuf or heap */
    int numkeys;                        /* Number of key indices return */
    int size;                           /* Available array size */
} get_keys_result_t;
typedef struct redis_client_t ;
typedef void redis_command_proc(struct redis_client_t *c);
typedef int redis_get_keys_proc(struct redis_command_t *cmd, latte_object_t **argv, int argc, get_keys_result_t *result);

typedef struct redis_command_t {
    char* name;
    redis_command_proc *proc;
    int arity;
    char* sflags;   /* Flags as string representation, one char per flag. */
    uint64_t flags; /* The actual flags, obtained from the 'sflags' field. */
    /* Use a function to determine keys arguments in a command line.
     * Used for Redis Cluster redirect. */
    redis_get_keys_proc *get_keys_proc;
    int firstkey;
    int lastkey;
    int keystep;
    long long microseconds, calls, rejected_calls, failed_calls;
    int id;
} redis_command_t;

typedef struct redis_client_t {
    latte_client_t client;
    int reqtype;            /* Request protocol type: PROTO_REQ_* */
    int multibulklen;       /* Number of multi bulk arguments left to read. */
    uint64_t flags;          /* Client flags: CLIENT_* macros. */
    int argc;               /* Num of arguments of current command. */
    latte_object_t **argv;            /* Arguments of current command. */
    size_t argv_len_sum;    /* Sum of lengths of objects in argv list. */
    long bulklen;           /* Length of bulk argument in multi bulk request. */
    list_t *reply;  
    unsigned long long reply_bytes;
    
    int bufpos;             /* Response buffer */
    char buf[PROTO_REPLY_CHUNK_BYTES];
    list_t* deferred_reply_errors;
    redis_command_t* cmd, lastcmd;
    long duration;
} redis_client_t;


latte_client_t* createRedisClient();
void freeRedisClient(latte_client_t* client);
void addReply(redis_client_t* c, latte_object_t* obj);
void addReplyError(latte_client_t *c, const char *err);
void addReplyErrorFormat(redis_client_t *c, const char *fmt, ...) ;

typedef struct shared_strings_objects_t {
    latte_object_t* ok;
} shared_strings_objects_t;
struct shared_strings_objects_t shared_strings;
void init_shared_strings();

#endif
