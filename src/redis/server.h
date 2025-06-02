#ifndef __REDIS_SERVER_H
#define __REDIS_SERVER_H
#include "server/server.h"
#include "config.h"
#include "sds/sds.h"
#include "dict/dict.h"
#include "command.h"
#include "db.h"

/* Server maxmemory strategies. Instead of using just incremental number
 * for this defines, we use a set of flags so that testing for certain
 * properties common to multiple policies is faster. */
#define MAXMEMORY_FLAG_LRU (1 << 0)
#define MAXMEMORY_FLAG_LFU (1 << 1)
#define MAXMEMORY_FLAG_ALLKEYS (1 << 2)
#define MAXMEMORY_FLAG_NO_SHARED_INTEGERS (MAXMEMORY_FLAG_LRU | MAXMEMORY_FLAG_LFU)

typedef struct redis_server_t {
    latte_server_t server;
    int exec_argc;
    sds* exec_argv;
    sds executable; /** execut file path **/
    sds configfile;
    config_manager_t* config;
    dict_t* commands;
    dict_t* robj_register;
    list_t* clients_to_close;
    time_t unixtime;
    int hz;
    struct redis_client_t* current_client;

    /** module */
    dict_t* modules;
    dict_t* module_api;
    
    /* config */
    long long proto_max_bulk_len;

    /* lru or lfu */
    int maxmemory_policy;
    unsigned int lruclock;

    /** db */
    struct redis_db_t* dbs;
    int db_num;
} redis_server_t;

void update_cache_time(struct latte_server_t* server);
int start_redis_server(redis_server_t* redis_server, int argc, sds* argv);
void register_commands(struct redis_server_t* redis_server);
redis_command_t* lookup_command(struct redis_server_t* server, sds command);
int init_redis_server_crons(redis_server_t* redis_server);
int init_redis_server_dbs(redis_server_t* redis_server);

#define OBJ_SHARED_BULKHDR_LEN 32
typedef struct shared_objects_t {
   latte_object_t* crlf, *ok, *pong, *wrongtypeerr,
   *mbulkhdr[OBJ_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
   *bulkhdr[OBJ_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
} shared_objects_t;
extern struct shared_objects_t shared;


/** commands */
void module_register_core_api(redis_server_t* server);
void module_help_command(redis_client_t* c);
void module_list_command(redis_client_t* c);
void module_load_command(redis_client_t* c);
void module_unload_command(redis_client_t* c);


#if __GNUC__ >= 4
#define redis_unreachable __builtin_unreachable
#else
#define redis_unreachable abort
#endif

#ifdef __GNUC__
void _redis_panic(const char *file, int line, const char *msg, ...)
    __attribute__ ((format (printf, 3, 4)));
#else
void _redis_panic(const char *file, int line, const char *msg, ...);
#endif
#define redis_panic(...) _redis_panic(__FILE__,__LINE__,__VA_ARGS__),redis_unreachable()

#endif