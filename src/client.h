#include "robj/robj.h"
#include "connection/connection.h"
#include "list/list.h"
#include "rax/rax.h"
#include "dict/dict.h"
#include "db.h"



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
/* #define CLIENT_... (1<<29) currently unused, feel free to use in the future */
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
#define CLIENT_NO_EVICT (1ULL<<43) /* This client is protected against client
                                      memory eviction. */


/* This structure represents a Redis user. This is useful for ACLs, the
 * user is associated to the connection after the connection is authenticated.
 * If there is no associated user, the connection uses the default user. */
#define USER_COMMAND_BITS_COUNT 1024    /* The total number of command bits
                                           in the user structure. The last valid
                                           command ID we can set in the user
                                           is USER_COMMAND_BITS_COUNT-1. */
#define USER_FLAG_ENABLED (1<<0)        /* The user is active. */
#define USER_FLAG_DISABLED (1<<1)       /* The user is disabled. */
#define USER_FLAG_ALLKEYS (1<<2)        /* The user can mention any key. */
#define USER_FLAG_ALLCOMMANDS (1<<3)    /* The user can run all commands. */
#define USER_FLAG_NOPASS      (1<<4)    /* The user requires no password, any
                                           provided password will work. For the
                                           default user, this also means that
                                           no AUTH is needed, and every
                                           connection is immediately
                                           authenticated. */
#define USER_FLAG_ALLCHANNELS (1<<5)    /* The user can mention any Pub/Sub
                                           channel. */
#define USER_FLAG_SANITIZE_PAYLOAD (1<<6)       /* The user require a deep RESTORE
                                                 * payload sanitization. */
#define USER_FLAG_SANITIZE_PAYLOAD_SKIP (1<<7)  /* The user should skip the
                                                 * deep sanitization of RESTORE
                                                 * payload. */

typedef struct {
    sds name;       /* The username as an SDS string. */
    uint64_t flags; /* See USER_FLAG_* */

    /* The bit in allowed_commands is set if this user has the right to
     * execute this command. In commands having subcommands, if this bit is
     * set, then all the subcommands are also available.
     *
     * If the bit for a given command is NOT set and the command has
     * subcommands, Redis will also check allowed_subcommands in order to
     * understand if the command can be executed. */
    uint64_t allowed_commands[USER_COMMAND_BITS_COUNT/64];

    /* This array points, for each command ID (corresponding to the command
     * bit set in allowed_commands), to an array of SDS strings, terminated by
     * a NULL pointer, with all the sub commands that can be executed for
     * this command. When no subcommands matching is used, the field is just
     * set to NULL to avoid allocating USER_COMMAND_BITS_COUNT pointers. */
    sds **allowed_subcommands;
    list *passwords; /* A list of SDS valid passwords for this user. */
    list *patterns;  /* A list of allowed key patterns. If this field is NULL
                        the user cannot mention any key in a command, unless
                        the flag ALLKEYS is set in the user. */
    list *channels;  /* A list of allowed Pub/Sub channel patterns. If this
                        field is NULL the user cannot mention any channel in a
                        `PUBLISH` or [P][UNSUBSCRIBE] command, unless the flag
                        ALLCHANNELS is set in the user. */
} user;


/* Client MULTI/EXEC state */
typedef struct multiCmd {
    robj **argv;
    int argc;
    struct redisCommand *cmd;
} multiCmd;

typedef struct multiState {
    multiCmd *commands;     /* Array of MULTI commands */
    int count;              /* Total number of MULTI commands */
    int cmd_flags;          /* The accumulated command flags OR-ed together.
                               So if at least a command has a given flag, it
                               will be set in this field. */
    int cmd_inv_flags;      /* Same as cmd_flags, OR-ing the ~flags. so that it
                               is possible to know if all the commands have a
                               certain flag. */
    size_t argv_len_sums;    /* mem used by all commands arguments */
} multiState;

typedef long long mstime_t; /* millisecond time type. */
typedef long long ustime_t; /* microsecond time type. */


/* This structure holds the blocking operation state for a client.
 * The fields used depend on client->btype. */
typedef struct blockingState {
    /* Generic fields. */
    long count;             /* Elements to pop if count was specified (BLMPOP), 0 otherwise. */
    mstime_t timeout;       /* Blocking operation timeout. If UNIX current time
                             * is > timeout then the operation timed out. */

    /* BLOCKED_LIST, BLOCKED_ZSET and BLOCKED_STREAM */
    dict *keys;             /* The keys we are waiting to terminate a blocking
                             * operation such as BLPOP or XREAD. Or NULL. */
    robj *target;           /* The key that should receive the element,
                             * for BLMOVE. */
    struct blockPos {
        int wherefrom;      /* Where to pop from */
        int whereto;        /* Where to push to */
    } blockpos;              /* The positions in the src/dst lists/zsets
                             * where we want to pop/push an element
                             * for BLPOP, BRPOP, BLMOVE and BZMPOP. */

    /* BLOCK_STREAM */
    size_t xread_count;     /* XREAD COUNT option. */
    robj *xread_group;      /* XREADGROUP group name. */
    robj *xread_consumer;   /* XREADGROUP consumer name. */
    int xread_group_noack;

    /* BLOCKED_WAIT */
    int numreplicas;        /* Number of replicas we are waiting for ACK. */
    long long reploffset;   /* Replication offset to reach. */

    /* BLOCKED_MODULE */
    void *module_blocked_handle; /* RedisModuleBlockedClient structure.
                                    which is opaque for the Redis core, only
                                    handled in module.c. */
} blockingState;

/* This callback type is called by moduleNotifyUserChanged() every time
 * a user authenticated via the module API is associated with a different
 * user or gets disconnected. This needs to be exposed since you can't cast
 * a function pointer to (void *). */
typedef void (*RedisModuleUserChangedFunc) (uint64_t client_id, void *privdata);

typedef struct {
    list *clients;
    size_t mem_usage_sum;
} clientMemUsageBucket;

#define CONFIG_RUN_ID_SIZE 40
#define PROTO_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */

typedef struct client {
    uint64_t id;            /* Client incremental unique ID. */
    connection *conn;
    int resp;               /* RESP protocol version. Can be 2 or 3. */
    redisDb *db;            /* Pointer to currently SELECTed DB. */
    robj *name;             /* As set by CLIENT SETNAME. */
    sds querybuf;           /* Buffer we use to accumulate client queries. */
    size_t qb_pos;          /* The position we have read in querybuf. */
    sds pending_querybuf;   /* If this client is flagged as master, this buffer
                               represents the yet not applied portion of the
                               replication stream that we are receiving from
                               the master. */
    size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size. */
    int argc;               /* Num of arguments of current command. */
    robj **argv;            /* Arguments of current command. */
    int original_argc;      /* Num of arguments of original command if arguments were rewritten. */
    robj **original_argv;   /* Arguments of original command if arguments were rewritten. */
    size_t argv_len_sum;    /* Sum of lengths of objects in argv list. */
    struct redisCommand *cmd, *lastcmd;  /* Last command executed. */
    user *user;             /* User associated with this connection. If the
                               user is set to NULL the connection can do
                               anything (admin). */
    int reqtype;            /* Request protocol type: PROTO_REQ_* */
    int multibulklen;       /* Number of multi bulk arguments left to read. */
    long bulklen;           /* Length of bulk argument in multi bulk request. */
    list *reply;            /* List of reply objects to send to the client. */
    unsigned long long reply_bytes; /* Tot bytes of objects in reply list. */
    size_t sentlen;         /* Amount of bytes already sent in the current
                               buffer or object being sent. */
    time_t ctime;           /* Client creation time. */
    long duration;          /* Current command duration. Used for measuring latency of blocking/non-blocking cmds */
    time_t lastinteraction; /* Time of the last interaction, used for timeout */
    time_t obuf_soft_limit_reached_time;
    uint64_t flags;         /* Client flags: CLIENT_* macros. */
    int authenticated;      /* Needed when the default user requires auth. */
    int replstate;          /* Replication state if this is a slave. */
    int repl_put_online_on_ack; /* Install slave write handler on first ACK. */
    int repldbfd;           /* Replication DB file descriptor. */
    off_t repldboff;        /* Replication DB file offset. */
    off_t repldbsize;       /* Replication DB file size. */
    sds replpreamble;       /* Replication DB preamble. */
    long long read_reploff; /* Read replication offset if this is a master. */
    long long reploff;      /* Applied replication offset if this is a master. */
    long long repl_ack_off; /* Replication ack offset, if this is a slave. */
    long long repl_ack_time;/* Replication ack time, if this is a slave. */
    long long repl_last_partial_write; /* The last time the server did a partial write from the RDB child pipe to this replica  */
    long long psync_initial_offset; /* FULLRESYNC reply offset other slaves
                                       copying this slave output buffer
                                       should use. */
    char replid[CONFIG_RUN_ID_SIZE+1]; /* Master replication ID (if master). */
    int slave_listening_port; /* As configured with: REPLCONF listening-port */
    char *slave_addr;       /* Optionally given by REPLCONF ip-address */
    int slave_capa;         /* Slave capabilities: SLAVE_CAPA_* bitwise OR. */
    multiState mstate;      /* MULTI/EXEC state */
    int btype;              /* Type of blocking op if CLIENT_BLOCKED. */
    blockingState bpop;     /* blocking state */
    long long woff;         /* Last write global replication offset. */
    list *watched_keys;     /* Keys WATCHED for MULTI/EXEC CAS */
    dict *pubsub_channels;  /* channels a client is interested in (SUBSCRIBE) */
    list *pubsub_patterns;  /* patterns a client is interested in (SUBSCRIBE) */
    sds peerid;             /* Cached peer ID. */
    sds sockname;           /* Cached connection target address. */
    listNode *client_list_node; /* list node in client list */
    listNode *paused_list_node; /* list node within the pause list */
    listNode *pending_read_list_node; /* list node in clients pending read list */
    RedisModuleUserChangedFunc auth_callback; /* Module callback to execute
                                               * when the authenticated user
                                               * changes. */
    void *auth_callback_privdata; /* Private data that is passed when the auth
                                   * changed callback is executed. Opaque for
                                   * Redis Core. */
    void *auth_module;      /* The module that owns the callback, which is used
                             * to disconnect the client if the module is
                             * unloaded for cleanup. Opaque for Redis Core.*/

    /* If this client is in tracking mode and this field is non zero,
     * invalidation messages for keys fetched by this client will be send to
     * the specified client ID. */
    uint64_t client_tracking_redirection;
    rax *client_tracking_prefixes; /* A dictionary of prefixes we are already
                                      subscribed to in BCAST mode, in the
                                      context of client side caching. */
    /* In updateClientMemUsage() we track the memory usage of
     * each client and add it to the sum of all the clients of a given type,
     * however we need to remember what was the old contribution of each
     * client, and in which category the client was, in order to remove it
     * before adding it the new value. */
    size_t last_memory_usage;
    int last_memory_type;

    size_t last_memory_usage_on_bucket_update;
    listNode *mem_usage_bucket_node;
    clientMemUsageBucket *mem_usage_bucket;

    /* Response buffer */
    int bufpos;
    size_t buf_usable_size; /* Usable size of buffer. */
    /* Note that 'buf' must be the last field of client struct, because memory
     * allocator may give us more memory than our apply for reducing fragments,
     * but we want to make full use of given memory, i.e. we may access the
     * memory after 'buf'. To avoid make others fields corrupt, 'buf' must be
     * the last one. */
    char buf[PROTO_REPLY_CHUNK_BYTES];
} client;

void addReplyErrorFormat(client *c, const char *fmt, ...);
void addReply(client *c, robj *obj);
void addReplySds(client *c, sds s);
void afterErrorReply(client *c, const char *s, size_t len);
void addReplyErrorObject(client *c, robj *err);
void addReplyHelp(client *c, const char **help);
void addReplyError(client *c, const char *err);
void freeClient(client *c);
client *createClient(connection *conn);


void selectDb(client* client, int index);

extern struct client* moduleFreeContextReusedClient;