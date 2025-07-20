
#include "server.h"
#include <string.h>
#include "debug/latte_debug.h"
#include "rax/rax.h"
#include "client.h"
/* OOM Score Adjustment classes. */
#define CONFIG_OOM_MASTER 0
#define CONFIG_OOM_REPLICA 1
#define CONFIG_OOM_BGCHILD 2
#define CONFIG_OOM_COUNT 3

extern int configOOMScoreAdjValuesDefaults[CONFIG_OOM_COUNT];

/* Hash table parameters */
#define HASHTABLE_MIN_FILL        10      /* Minimal hash table fill 10% */
#define HASHTABLE_MAX_LOAD_FACTOR 1.618   /* Maximum hash table load factor. */

/* Command flags. Please check the command table defined in the server.c file
 * for more information about the meaning of every flag. */
#define CMD_WRITE (1ULL<<0)            /* "write" flag */
#define CMD_READONLY (1ULL<<1)         /* "read-only" flag */
#define CMD_DENYOOM (1ULL<<2)          /* "use-memory" flag */
#define CMD_MODULE (1ULL<<3)           /* Command exported by module. */
#define CMD_ADMIN (1ULL<<4)            /* "admin" flag */
#define CMD_PUBSUB (1ULL<<5)           /* "pub-sub" flag */
#define CMD_NOSCRIPT (1ULL<<6)         /* "no-script" flag */
#define CMD_RANDOM (1ULL<<7)           /* "random" flag */
#define CMD_SORT_FOR_SCRIPT (1ULL<<8)  /* "to-sort" flag */
#define CMD_LOADING (1ULL<<9)          /* "ok-loading" flag */
#define CMD_STALE (1ULL<<10)           /* "ok-stale" flag */
#define CMD_SKIP_MONITOR (1ULL<<11)    /* "no-monitor" flag */
#define CMD_SKIP_SLOWLOG (1ULL<<12)    /* "no-slowlog" flag */
#define CMD_ASKING (1ULL<<13)          /* "cluster-asking" flag */
#define CMD_FAST (1ULL<<14)            /* "fast" flag */
#define CMD_NO_AUTH (1ULL<<15)         /* "no-auth" flag */
#define CMD_MAY_REPLICATE (1ULL<<16)   /* "may-replicate" flag */

/* Command flags used by the module system. */
#define CMD_MODULE_GETKEYS (1ULL<<17)  /* Use the modules getkeys interface. */
#define CMD_MODULE_NO_CLUSTER (1ULL<<18) /* Deny on Redis Cluster. */

/* Command flags that describe ACLs categories. */
#define CMD_CATEGORY_KEYSPACE (1ULL<<19)
#define CMD_CATEGORY_READ (1ULL<<20)
#define CMD_CATEGORY_WRITE (1ULL<<21)
#define CMD_CATEGORY_SET (1ULL<<22)
#define CMD_CATEGORY_SORTEDSET (1ULL<<23)
#define CMD_CATEGORY_LIST (1ULL<<24)
#define CMD_CATEGORY_HASH (1ULL<<25)
#define CMD_CATEGORY_STRING (1ULL<<26)
#define CMD_CATEGORY_BITMAP (1ULL<<27)
#define CMD_CATEGORY_HYPERLOGLOG (1ULL<<28)
#define CMD_CATEGORY_GEO (1ULL<<29)
#define CMD_CATEGORY_STREAM (1ULL<<30)
#define CMD_CATEGORY_PUBSUB (1ULL<<31)
#define CMD_CATEGORY_ADMIN (1ULL<<32)
#define CMD_CATEGORY_FAST (1ULL<<33)
#define CMD_CATEGORY_SLOW (1ULL<<34)
#define CMD_CATEGORY_BLOCKING (1ULL<<35)
#define CMD_CATEGORY_DANGEROUS (1ULL<<36)
#define CMD_CATEGORY_CONNECTION (1ULL<<37)
#define CMD_CATEGORY_TRANSACTION (1ULL<<38)
#define CMD_CATEGORY_SCRIPTING (1ULL<<39)

/* swap datatype flags*/
#define CMD_SWAP_DATATYPE_KEYSPACE (1ULL<<40)
#define CMD_SWAP_DATATYPE_STRING (1ULL<<41)
#define CMD_SWAP_DATATYPE_HASH (1ULL<<42)
#define CMD_SWAP_DATATYPE_SET (1ULL<<43)
#define CMD_SWAP_DATATYPE_ZSET (1ULL<<44)
#define CMD_SWAP_DATATYPE_LIST (1ULL<<45)
#define CMD_SWAP_DATATYPE_BITMAP (1ULL<<46)

#define SWAP_UNSET -1
#define SWAP_NOP    0
#define SWAP_IN     1
#define SWAP_OUT    2
#define SWAP_DEL    3
#define SWAP_UTILS  4
#define SWAP_TYPES  5

typedef struct  acl_category_item_t {
    const char *name;
    uint64_t flag;
} acl_category_item_t;



struct acl_category_item_t acl_command_categories[] = {
    {"write", CMD_WRITE|CMD_CATEGORY_WRITE},
    {"read-only", CMD_READONLY|CMD_CATEGORY_READ},
    {"use-memory", CMD_DENYOOM},
    {"admin", CMD_ADMIN|CMD_CATEGORY_ADMIN|CMD_CATEGORY_DANGEROUS},
    {"pub-sub", CMD_PUBSUB|CMD_CATEGORY_PUBSUB},
    {"no-script", CMD_NOSCRIPT},
    {"random", CMD_RANDOM},
    {"to-sort", CMD_SORT_FOR_SCRIPT},
    {"ok-loading", CMD_LOADING},
    {"ok-stale", CMD_STALE},
    {"no-monitor", CMD_SKIP_MONITOR},
    {"no-slowlog", CMD_SKIP_SLOWLOG},
    {"cluster-asking", CMD_ASKING},
    {"fast", CMD_FAST | CMD_CATEGORY_FAST},
    {"no-auth", CMD_NO_AUTH},
    {"may-replicate", CMD_MAY_REPLICATE},
    {"@keyspace", CMD_CATEGORY_KEYSPACE},
    {"@read", CMD_CATEGORY_READ},
    {"@write", CMD_CATEGORY_WRITE},
    {"@set", CMD_CATEGORY_SET},
    {"@sortedset", CMD_CATEGORY_SORTEDSET},
    {"@list", CMD_CATEGORY_LIST},
    {"@hash", CMD_CATEGORY_HASH},
    {"@string", CMD_CATEGORY_STRING},
    {"@bitmap", CMD_CATEGORY_BITMAP},
    {"@hyperloglog", CMD_CATEGORY_HYPERLOGLOG},
    {"@geo", CMD_CATEGORY_GEO},
    {"@stream", CMD_CATEGORY_STREAM},
    {"@pubsub", CMD_CATEGORY_PUBSUB},
    {"@admin", CMD_CATEGORY_ADMIN},
    {"@fast", CMD_CATEGORY_FAST},
    {"@slow", CMD_CATEGORY_SLOW},
    {"@blocking", CMD_CATEGORY_BLOCKING},
    {"@dangerous", CMD_CATEGORY_DANGEROUS},
    {"@connection", CMD_CATEGORY_CONNECTION},
    {"@transaction", CMD_CATEGORY_TRANSACTION},
    {"@scripting", CMD_CATEGORY_SCRIPTING},
    /* swap */
    {"@swap_keyspace", CMD_SWAP_DATATYPE_KEYSPACE},
    {"@swap_string", CMD_SWAP_DATATYPE_STRING},
    {"@swap_hash", CMD_SWAP_DATATYPE_HASH},
    {"@swap_set", CMD_SWAP_DATATYPE_SET},
    {"@swap_zset", CMD_SWAP_DATATYPE_ZSET},
    {"@swap_list", CMD_SWAP_DATATYPE_LIST},
    {"@swap_bitmap", CMD_SWAP_DATATYPE_BITMAP},
    {NULL,0} /* Terminator. */
};

/* Given the category name the command returns the corresponding flag, or
 * zero if there is no match. */
uint64_t command_data_type_flag_by_name(const char *name) {
    for (int j = 0; acl_command_categories[j].flag != 0; j++) {
        if (!strcasecmp(name,acl_command_categories[j].name)) {
            return acl_command_categories[j].flag;
        }
    }
    return 0; /* No match. */
}

/* Parse the flags string description 'strflags' and set them to the
 * command 'c'. If the flags are all valid C_OK is returned, otherwise
 * C_ERR is returned (yet the recognized flags are set in the command). */
int populate_command_table_parse_flags(struct redis_command_t *c, char *strflags) {
    int argc;
    sds *argv;
    int catflag;
    /* Split the line into arguments for processing. */
    argv = sds_split_args(strflags,&argc);
    if (argv == NULL) return -1;

    for (int j = 0; j < argc; j++) {
        char *flag = argv[j];
        
        if((catflag = command_data_type_flag_by_name(flag)) != 0) {
            c->flags |= catflag;
        } else {
            sds_free_splitres(argv,argc);
            return -1;
        }
    }
    /* If it's not @fast is @slow in this binary world. */
    if (!(c->flags & CMD_CATEGORY_FAST)) c->flags |= CMD_CATEGORY_SLOW;

    sds_free_splitres(argv,argc);
    return 0;
}

void quit_command(redis_client_t* c) {
    add_reply(c, shared.ok);
    c->client.flags |= CLIENT_CLOSE_AFTER_REPLY;
}



void ping_command(redis_client_t* c) {
    if (c->argc > 2) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }
    if (c->argc == 1) {
        add_reply(c, shared.pong);
    } else {
        add_reply_bulk(c, c->argv[1]);
    }
    
}


void module_command(redis_client_t* c) {
    char* subcmd = c->argv[1]->ptr;
    if (c->argc == 2 && !strcasecmp(subcmd, "help")) {
        module_help_command(c);
    } else if (!strcasecmp(subcmd,"load") && c->argc >= 3) {
        module_load_command(c);
    } else if (!strcasecmp(subcmd,"unload") && c->argc >= 3) {
        module_unload_command(c);
    } else if (!strcasecmp(subcmd,"list") && c->argc >= 3) {
        module_list_command(c);
    } else {
        // add_reply_subcommand_syntax_error(c); 
    }
}

struct redis_command_t redis_command_table[] = {
    {
        "quit", quit_command, 0,
        "admin",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "module", module_command, -2,
        "admin no-script",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "ping", ping_command, -2,
        "admin",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    }
};


static rax *commandId;
static unsigned long nextid;
#define USER_COMMAND_BITS_COUNT 1024
/* For ACL purposes, every user has a bitmap with the commands that such
 * user is allowed to execute. In order to populate the bitmap, every command
 * should have an assigned ID (that is used to index the bitmap). This function
 * creates such an ID: it uses sequential IDs, reusing the same ID for the same
 * command name, so that a command retains the same ID in case of modules that
 * are unloaded and later reloaded. */
unsigned long acl_get_command_id(const char *cmdname) {

    sds lowername = sds_new(cmdname);
    sds_to_lower(lowername);
    if (commandId == NULL) commandId = raxNew();
    void *id;
    if (raxFind(commandId,(unsigned char*)lowername,sds_len(lowername),&id)) {
        sds_delete(lowername);
        return (unsigned long)id;
    }
    raxInsert(commandId,(unsigned char*)lowername,strlen(lowername),
              (void*)nextid,NULL);
    sds_delete(lowername);
    unsigned long thisid = nextid;
    nextid++;

    /* We never assign the last bit in the user commands bitmap structure,
     * this way we can later check if this bit is set, understanding if the
     * current ACL for the user was created starting with a +@all to add all
     * the possible commands and just subtracting other single commands or
     * categories, or if, instead, the ACL was created just adding commands
     * and command categories from scratch, not allowing future commands by
     * default (loaded via modules). This is useful when rewriting the ACLs
     * with ACL SAVE. */
    if (nextid == USER_COMMAND_BITS_COUNT-1) nextid++;
    return thisid;
}

void register_commands(redis_server_t* rs) {
    int j;
    int num_commands = sizeof(redis_command_table)/sizeof(struct redis_command_t);

    for (j = 0; j < num_commands; j++) {
        struct redis_command_t *c = redis_command_table + j;
        int retval1, retval2;
        //string -> int
        if (populate_command_table_parse_flags(c,  c->sflags) == -1) {
            latte_panic("unsupported command flag");
        }

        c->id = acl_get_command_id(c->name);
        retval1 = dict_add(rs->commands, sds_new(c->name), c);
        LATTE_LIB_LOG(LL_INFO, "register %s command", c->name);
    }
}

struct redis_command_t* lookup_command(redis_server_t* server, sds command) {
    return dict_fetch_value(server->commands, command);
}