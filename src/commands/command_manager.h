#ifndef __COMMAND_MANAGER_H
#define __COMMAND_MANAGER_H


#include "../redis/command.h"
#include <string.h>
#include "debug/latte_debug.h"
#include "rax/rax.h"
#include "../redis/client.h"
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


typedef struct command_manager_t {
    dict_t* commands;
    rax *commandId;
    unsigned long nextid;
} command_manager_t;

command_manager_t* command_manager_new();
void command_manager_delete(command_manager_t* cm);
redis_command_t* command_manager_lookup(command_manager_t* cm, sds command);
int command_manager_register_command(command_manager_t* cm, redis_command_t* cmd);

//commands 
void ping_command(redis_client_t* c);
void quit_command(redis_client_t* c);
void module_command(redis_client_t* c);
void info_command(redis_client_t* c);

#endif