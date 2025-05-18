#ifndef __REDIS_SERVER_H
#define __REDIS_SERVER_H
#include "server/server.h"
#include "config.h"
#include "sds/sds.h"
#include "dict/dict.h"
#include "command.h"
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
    latteAtomic time_t unixtime;

    struct redis_client_t* current_client;

    /** module */
    dict_t* modules;
    dict_t* module_api;
    
    /* config */
    long long proto_max_bulk_len;
} redis_server_t;


int start_redis_server(redis_server_t* redis_server, int argc, sds* argv);
void register_commands(struct redis_server_t* redis_server);
redis_command_t* lookup_command(struct redis_server_t* server, sds command);

#define OBJ_SHARED_BULKHDR_LEN 32
typedef struct shared_objects_t {
   latte_object_t* crlf, *ok, *pong,
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