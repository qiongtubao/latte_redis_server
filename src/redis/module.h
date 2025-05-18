#ifndef __REDIS_MODULE_H
#define __REDIS_MODULE_H

#include "server.h"
#include "client.h"

typedef void (*redis_module_info_func)(struct redis_module_info_ctx *ctx, int for_crash_report);
typedef void (*redis_module_defrag_func)(struct RedisModuleDefragCtx *ctx);

typedef struct redis_module_t {
    void* handle;
    char* name;
    int ver;
    int apiver;
    list_t* types;
    list_t* usedby;
    list_t* using;
    list_t* filters;
    int in_call;
    int in_hook;
    int options;
    int blocked_clients;
    redis_module_info_func info_cb;
    redis_module_defrag_func defrag_cb;
} redis_module_t;

typedef struct redis_module_info_ctx {
    redis_module_t* module;
    const char *requested_section;
    sds info;
    int sections;
    int in_section;
    int in_dict_field;
} redis_module_info_ctx;

typedef struct redis_module_ctx_t {
    void* getapifuncptr;
    redis_server_t* server;
    redis_module_t *module;
    redis_client_t* client; // call command
    // struct redis_module_blocked_client_t* blocked_client;
    // struct auto_mem_entry_t* amqueue;
    // int amqueue_len;
    // int amqueue_used;
    int flags;
    // void **postponed_arrays;
    // int postponed_arrays_count;
    // void* blocked_privdata;
    // redis_module_string* blocked_ready_key;

} redis_module_ctx_t;


// #define REDIS_MODULE_CTX_INIT {\
//     (void*)(unsigned long)&RM_GetApi, \
//     NULL, \
//     NULL, \
//     NULL, \
//     NULL, 
//     0,\
//     0,\ 
//     0,\
//     NULL,\
//     0, \
//     NULL,\
//     NULL,\
//     NULL,\
//     NULL,\
//     {0} \
// }

typedef struct get_key_requests_result_t {
    int num;
    int size;
} get_key_requests_result_t;
typedef int (*redis_module_cmd_func) (redis_module_ctx_t* ctx, void **argv, int argc);
typedef int (*redis_module_get_swaps_func) (int dbid, redis_module_ctx_t *ctx, latte_object_t **argv, int argc, get_key_requests_result_t *result);
typedef struct redis_module_command_proxy_t {
    redis_module_t *module;
    redis_module_cmd_func func;
    redis_command_t* redis_cmd;
} redis_module_command_proxy_t;



#endif