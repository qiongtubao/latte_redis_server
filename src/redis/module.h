#ifndef __REDIS_MODULE_H
#define __REDIS_MODULE_H

#include "server.h"
#include "client.h"

typedef void (*redis_module_info_func)(struct redis_module_info_ctx *ctx, int for_crash_report);
typedef void (*redis_module_defrag_func)(struct RedisModuleDefragCtx *ctx);


/**
 * 模块条目结构体
 * 存储模块的路径和参数信息
 */
typedef struct module_entry_t {
    sds path;        /* 模块文件路径 */
    vector_t* args;  /* 模块启动参数数组 */
} module_entry_t;

module_entry_t* module_entry_new(sds path, int argc, char** args);
void module_entry_delete(void* data);

/**
 * Redis 模块结构体
 * 存储模块的所有状态和属性信息
 */
typedef struct redis_module_t {
    void* handle;                           /* 动态库句柄 */
    char* name;                             /* 模块名称 */
    int ver;                                /* 模块版本号 */
    int apiver;                             /* API 版本号 */
    list_t* types;                          /* 模块定义的数据类型列表 */
    list_t* usedby;                         /* 使用此模块的模块列表 */
    list_t* using;                          /* 此模块使用的模块列表 */
    list_t* filters;                        /* 过滤器列表 */
    int in_call;                            /* 是否正在调用中 */
    int in_hook;                            /* 是否在钩子中 */
    int options;                            /* 模块选项标志位 */
    int blocked_clients;                    /* 阻塞的客户端数量 */
    redis_module_info_func info_cb;         /* 信息回调函数 */
    redis_module_defrag_func defrag_cb;     /* 内存碎片整理回调函数 */
} redis_module_t;

/**
 * Redis 模块信息上下文结构体
 * 用于模块信息获取和处理
 */
typedef struct redis_module_info_ctx {
    redis_module_t* module;        /* 关联的模块 */
    const char *requested_section; /* 请求的信息段名称 */
    sds info;                      /* 信息字符串缓冲区 */
    int sections;                  /* 段落数量 */
    int in_section;                /* 是否在段落中 */
    int in_dict_field;             /* 是否在字典字段中 */
} redis_module_info_ctx;

/**
 * Redis 模块上下文结构体
 * 提供模块与 Redis 核心交互的上下文环境
 */
typedef struct redis_module_ctx_t {
    void* getapifuncptr;               /* 获取 API 函数指针的函数 */
    redis_server_t* server;            /* Redis 服务器实例 */
    redis_module_t *module;            /* 当前模块实例 */
    redis_client_t* client;            /* 执行命令的客户端 */
    // struct redis_module_blocked_client_t* blocked_client;
    // struct auto_mem_entry_t* amqueue;
    // int amqueue_len;
    // int amqueue_used;
    int flags;                         /* 上下文标志位 */
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

/**
 * 获取键请求结果结构体
 * 用于存储键请求操作的结果信息
 */
typedef struct get_key_requests_result_t {
    int num;    /* 键请求数量 */
    int size;   /* 结果缓冲区大小 */
} get_key_requests_result_t;
typedef int (*redis_module_cmd_func) (redis_module_ctx_t* ctx, void **argv, int argc);
typedef int (*redis_module_get_swaps_func) (int dbid, redis_module_ctx_t *ctx, latte_object_t **argv, int argc, get_key_requests_result_t *result);
/**
 * Redis 模块命令代理结构体
 * 用于将模块命令与 Redis 核心命令系统连接
 */
typedef struct redis_module_command_proxy_t {
    redis_module_t *module;        /* 所属模块 */
    redis_module_cmd_func func;    /* 模块命令处理函数 */
    redis_command_t* redis_cmd;    /* 对应的 Redis 命令结构 */
} redis_module_command_proxy_t;



#endif