


#include "module.h"
#include <dlfcn.h>
#include <sys/stat.h>
#include <string.h>
#define REDISMODULE_CORE 1
#include "redis_module.h"
#include "../object/string.h"
#include "object/object_manager.h"
#include "debug/latte_debug.h"
#include "../shared/shared.h"
#include "utils/utils.h"

/* Redis 模块上下文标志位定义 */
#define REDIS_MODULE_CTX_AUTO_MEMORY (1<<0)              /* 自动内存管理标志 */
#define REDIS_MODULE_CTX_KEYS_POS_REQUEST (1<<1)         /* 键位置请求标志 */
#define REDIS_MODULE_CTX_BLOCKED_REPLY (1<<2)            /* 阻塞回复标志 */
#define REDIS_MODULE_CTX_BLOCKED_TIMEOUT (1<<3)          /* 阻塞超时标志 */
#define REDIS_MODULE_CTX_THREAD_SAFE (1<<4)              /* 线程安全标志 */
#define REDIS_MODULE_CTX_BLOCKED_DISCONNECTED (1<<5)     /* 阻塞断开连接标志 */
#define REDIS_MODULE_CTX_MODULE_COMMAND_CALL (1<<6)      /* 模块命令调用标志 */
#define REDIS_MODULE_CTX_MULTI_EMITTED (1<<7)            /* 多命令发出标志 */

/**
 * 显示 MODULE 命令的帮助信息
 * 输入: redis_client_t* c - 客户端连接
 * 返回: 无
 */
void module_help_command(redis_client_t* c) {
    const char *help[] = {
        "LIST",
        "  Return a list of loaded modules.",
        "LOAD <path> [<arg> ...]",
        "  Load a module library from <path>, passing to it any optional arguments.",
        "UNLOAD <name>",
        "  Unload a module.",
        NULL
    };
    add_reply_help(c, help);
}

static redis_client_t *module_free_context_reused_client;
/**
 * 从 module_api 字典获取指定名称的 API 函数指针
 * 输入: ctx - 模块上下文, funcname - 函数名称, ptr - 输出函数指针
 * 返回: 成功返回 0, 失败返回 -1
 */
int redis_module_use_get_api(redis_module_ctx_t* ctx, const char* funcname, void **ptr) {
    dict_entry_t* he = dict_find(ctx->server->module_api, funcname);
    if (!he) return -1;
    *ptr = dict_get_entry_val(he);
    return 0;
}

#define REDIS_MODULE_CTX_INIT {(void*)(unsigned long)&redis_module_use_get_api,NULL,NULL,0}
/**
 * 动态加载 .so 模块文件
 * 功能: 检查文件权限→使用 dlopen 加载→通过 dlsym 获取 onload 函数→调用 onload 进行初始化→注册模块
 * 输入: server - Redis服务器实例, path - 模块文件路径, module_argv - 模块参数, module_argc - 参数数量
 * 返回: 成功返回 0, 失败返回 -1
 */
int module_load(redis_server_t* server,const char *path, void **module_argv, int module_argc) {
    int (*onload)(void *, void **, int);
    void* handle;
    redis_module_ctx_t ctx = REDIS_MODULE_CTX_INIT;
    ctx.server = server;
    LATTE_LIB_LOG(LOG_WARN, "module load set server module_api %p", server->module_api);
    // ctx.client = module_free_context_reused_client;
    // selectDb(ctx.client, 0);

    struct stat st;
    // 检查模块文件是否存在和具有执行权限
    if (stat(path, &st) == 0) {
        if (!(st.st_mode & (S_IXUSR  | S_IXGRP | S_IXOTH))) {
            LATTE_LIB_LOG(LOG_WARN, "Module %s failed to load: It does not have execute permissions.", path);
            return -1;
        }
    }
    // 动态打开 .so 文件
    handle = dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if (handle == NULL) {
        LATTE_LIB_LOG(LOG_WARN, "Module %s failed to load: %s", path, dlerror());
        return -1;
    }
    // 获取模块的 redis_module_onload 入口函数
    onload = (int (*)(void *, void **, int))(unsigned long) dlsym(handle,"redis_module_onload");
    if (onload == NULL) {
        dlclose(handle);
        LATTE_LIB_LOG(LOG_WARN,
            "Module %s does not export RedisModule_OnLoad() "
            "symbol. Module not loaded.",path);
        return -1;
    }
    // 调用模块的初始化函数，加载失败时清理资源
    if (onload((void*)&ctx, module_argv, module_argc) == -1) {
        if (ctx.module) {
            // module_unregister_commands(ctx.module);
            // module_unregister_shared_api(ctx.module);
            // module_unregister_used_api(ctx.module);
            // module_free_module_structure(ctx.module);
        }
        dlclose(handle);
        LATTE_LIB_LOG(LOG_WARN, "Module %s initialization failed. Module not loaded",path);
        return -1;
    }
    dict_add(server->modules, ctx.module->name, ctx.module);
    // ctx.module->blocked_clients = 0;
    // ctx.module->handle = handle;
    // LATTE_LIB_LOG(LL_DEBUG,"Module '%s' loaded from %s",ctx.module->name,path);

    // module_fire_server_event(REDISMODULE_EVENT_MODULE_CHANGE,
    //                       REDISMODULE_SUBEVENT_MODULE_LOADED,
    //                     ctx.module);
    // module_free_context(&ctx);
    return 0;
}

/**
 * MODULE LOAD 命令处理函数
 * 输入: c - 客户端连接
 * 返回: 无
 */
void module_load_command(redis_client_t* c) {
    latte_object_t **argv = NULL;
    int argc = 0;

    if (c->argc > 3) {
        argc = c->argc - 3;
        argv = &c->argv[3];
    }

    if (module_load(c->client.server, c->argv[2]->ptr,(void **)argv,argc) == 0)
        add_reply(c,shared.ok);
    else
        add_reply_error(c,
            "Error loading the extension. Please check the server logs.");
}

/**
 * MODULE UNLOAD 命令处理函数
 * 输入: c - 客户端连接
 * 返回: 无
 */
void module_unload_command(redis_client_t* c) {

}

/**
 * MODULE LIST 命令处理函数
 * 输入: c - 客户端连接
 * 返回: 无
 */
void module_list_command(redis_client_t* c) {

}


/**
 * 注册单个模块 API 函数
 * 输入: server - Redis服务器实例, funname - 函数名, funcptr - 函数指针
 * 返回: 注册结果
 */
int module_register_api(redis_server_t* server ,const char *funname, void *funcptr) {
    return dict_add(server->module_api, (char*)funname, funcptr);
}

/* 批量注册 API 函数的宏定义 */
#define REGISTER_API(server, name) \
    module_register_api(server, "redis_module_" #name, (void *)(unsigned long)redis_module_use_ ## name)



/* 模块 API 字典类型定义，使用纯 C 字符串，因为需要被模块查询 */

/**
 * C字符串键的哈希函数
 * 输入: key - 键值
 * 返回: 哈希值
 */
uint64_t dict_cstring_key_hash(const void *key) {
    return dict_gen_hash_function((unsigned char*)key, strlen((char*)key));
}

/**
 * C字符串键的比较函数
 * 输入: privdata - 私有数据, key1/key2 - 比较的键
 * 返回: 相等返回1，不等返回0
 */
int dict_cstring_key_compare(dict_t *privdata, const void *key1, const void *key2) {
    UNUSED(privdata);
    return strcmp(key1,key2) == 0;
}


dict_func_t module_api_dict_type = {
    dict_cstring_key_hash,        /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dict_cstring_key_compare,     /* key compare */
    NULL,                      /* key destructor */
    NULL,                      /* val destructor */
    NULL                       /* allow to expand */
};


/**
 * 内存分配代理函数 - malloc
 * 输入: bytes - 分配字节数
 * 返回: 内存指针
 */
void* redis_module_use_malloc(size_t bytes) {
    return zmalloc(bytes);
}

/**
 * 内存分配代理函数 - calloc
 * 输入: nmemb - 元素数量, size - 元素大小
 * 返回: 清零的内存指针
 */
void* redis_module_use_calloc(size_t nmemb, size_t size) {
    return zcalloc(nmemb*size);
}

/**
 * 内存重分配代理函数 - realloc
 * 输入: ptr - 原内存指针, bytes - 新大小
 * 返回: 重分配后的内存指针
 */
void* redis_module_use_realloc(void* ptr, size_t bytes) {
    return zrealloc(ptr, bytes);
}

/**
 * 内存释放代理函数 - free
 * 输入: ptr - 要释放的内存指针
 * 返回: 无
 */
void redis_module_use_free(void* ptr) {
    zfree(ptr);
}

void module_free_context(redis_module_ctx_t* ctx) {

}

/**
 * 模块命令分发器
 * 功能: 将 redis_client_t 转换为 module_ctx，然后执行模块命令函数
 * 输入: c - Redis 客户端连接
 * 返回: 无
 */
void redis_module_command_dispatcher(redis_client_t* c) {
    redis_module_command_proxy_t* cp = (void*)(unsigned long)c->cmd->get_keys_proc;
    redis_module_ctx_t ctx = REDIS_MODULE_CTX_INIT;
    ctx.flags |=  REDIS_MODULE_CTX_MODULE_COMMAND_CALL;
    ctx.module = cp->module;
    ctx.server = c->client.server;
    ctx.client = c;
    cp->func(&ctx, (void**)c->argv, c->argc);
    module_free_context(&ctx);

    /* In some cases processMultibulkBuffer uses sdsMakeRoomFor to
     * expand the query buffer, and in order to avoid a big object copy
     * the query buffer SDS may be used directly as the SDS string backing
     * the client argument vectors: sometimes this will result in the SDS
     * string having unused space at the end. Later if a module takes ownership
     * of the RedisString, such space will be wasted forever. Inside the
     * Redis core this is not a problem because tryObjectEncoding() is called
     * before storing strings in the key space. Here we need to do it
     * for the module. */

    // for (int i = 0; i < c->argc; i++) {
    //     /* Only do the work if the module took ownership of the object:
    //      * in that case the refcount is no longer 1. */
    //     if (c->argv[i]->refcount > 1)
    //         trimStringObjectIfNeeded(c->argv[i]);
    // }
}

/**
 * 将命令标志字符串解析为 flags 位掩码
 * 输入: s - 标志字符串（空格分隔）
 * 返回: 成功返回 flags 位掩码，失败返回 -1
 */
int64_t command_flags_from_string(char* s) {
    int count, j;
    int64_t flags = 0;
    sds *tokens = sds_split_len(s, strlen(s), " ", 1, &count);
    for (j = 0; j < count; j++) {
        char* t = tokens[j];
        if (!strcasecmp(t,"write")) flags |= CMD_WRITE;
        else if (!strcasecmp(t,"readonly")) flags |= CMD_READONLY;
        else if (!strcasecmp(t,"admin")) flags |= CMD_ADMIN;
        else if (!strcasecmp(t,"deny-oom")) flags |= CMD_DENYOOM;
        else if (!strcasecmp(t,"deny-script")) flags |= CMD_NOSCRIPT;
        else if (!strcasecmp(t,"allow-loading")) flags |= CMD_LOADING;
        else if (!strcasecmp(t,"pubsub")) flags |= CMD_PUBSUB;
        else if (!strcasecmp(t,"random")) flags |= CMD_RANDOM;
        else if (!strcasecmp(t,"allow-stale")) flags |= CMD_STALE;
        else if (!strcasecmp(t,"no-monitor")) flags |= CMD_SKIP_MONITOR;
        else if (!strcasecmp(t,"no-slowlog")) flags |= CMD_SKIP_SLOWLOG;
        else if (!strcasecmp(t,"fast")) flags |= CMD_FAST;
        else if (!strcasecmp(t,"no-auth")) flags |= CMD_NO_AUTH;
        else if (!strcasecmp(t,"may-replicate")) flags |= CMD_MAY_REPLICATE;
        else if (!strcasecmp(t,"getkeys-api")) flags |= CMD_MODULE_GETKEYS;
        else if (!strcasecmp(t,"no-cluster")) flags |= CMD_MODULE_NO_CLUSTER;
        else if (!strcasecmp(t,"swap-nop")) continue;
        else if (!strcasecmp(t,"swap-get")) continue;
        else if (!strcasecmp(t,"swap-put")) continue;
        else if (!strcasecmp(t,"swap-del")) continue;
        else break;
    }
    sds_free_splitres(tokens, count);
    if (j != count) return -1;
    return flags;
}

/**
 * 注册模块命令到 Redis 命令系统
 * 输入: ctx - 模块上下文, name - 命令名, cmdfunc - 命令处理函数, getkeyrequests_func - 获取键请求函数, strflags - 标志字符串, firstkey/lastkey/keystep - 键参数位置信息
 * 返回: 成功返回 0, 失败返回 -1
 */
int redis_module_use_create_command(redis_module_ctx_t* ctx, const char* name,  redis_module_cmd_func cmdfunc, redis_module_get_swaps_func getkeyrequests_func, const char *strflags, int firstkey, int lastkey, int keystep) {
    int64_t flags = strflags ? command_flags_from_string((char*)strflags): 0;
    if (flags == -1) return -1;
//     if ((flags & CMD_MODULE_NO_CLUSTER) && server.cluster_enabled)
//         return -1;

    // int intention = strflags ? swap_action_from_string((char*)strflags) : 0;
    // if (intention == -1) return -1;

    struct redis_command_t *rediscmd;
    redis_module_command_proxy_t *cp;

    sds cmdname = sds_new(name);

    if (command_manager_lookup(ctx->server->command_manager, cmdname) != NULL) {
        sds_delete(cmdname);
        return -1;
    }
    redis_get_key_requests_proc_func get_key_requests_proc = (redis_get_key_requests_proc_func)getkeyrequests_func;
    cp = zmalloc(sizeof(*cp));
    cp->module = ctx->module;
    cp->func = cmdfunc;
    cp->redis_cmd = zmalloc(sizeof(*rediscmd));
    cp->redis_cmd->name = cmdname;
    cp->redis_cmd->proc = redis_module_command_dispatcher;  //对外执行函数
    cp->redis_cmd->arity = -1;
    cp->redis_cmd->flags = flags | CMD_MODULE;
    cp->redis_cmd->intention = 0;
    cp->redis_cmd->get_keys_proc = (redis_get_keys_proc_func*)(unsigned long)cp;
    cp->redis_cmd->get_key_requests_proc = &get_key_requests_proc;
    cp->redis_cmd->firstkey = firstkey;
    cp->redis_cmd->lastkey = lastkey;
    cp->redis_cmd->keystep = keystep;
    cp->redis_cmd->microseconds = 0;
    cp->redis_cmd->calls = 0;
    cp->redis_cmd->rejected_calls = 0;
    cp->redis_cmd->failed_calls = 0;
    // dict_add(ctx->server->orig_commands, sds_dup(cmdname), cp->redis_cmd);
    command_manager_register_command(ctx->server->command_manager, cp->redis_cmd);
    return 0;
}

/**
 * 检查模块名是否已被占用
 * 输入: ctx - 模块上下文, name - 模块名
 * 返回: 已占用返回非零值，未占用返回 0
 */
int redis_module_use_is_module_name_busy(redis_module_ctx_t* ctx,const char *name) {
    sds modulename = sds_new(name);
    dict_entry_t *de = dict_find(ctx->server->modules,modulename);
    sds_delete(modulename);
    return de != NULL;
}


redis_client_t* module_get_reply_client(redis_module_ctx_t* ctx) {
    return ctx->client;
}

int redis_module_use_reply_with_simple_string(redis_module_ctx_t *ctx, const char* msg) {
    latte_client_t* c = module_get_reply_client(ctx);
    if (c == NULL) return 0;
    add_reply_proto(c, "+", 1);
    add_reply_proto(c, msg, strlen(msg));
    add_reply_proto(c, "\r\n", 2);
    return 0;
}

dict_entry_t* redis_module_use_lookup_key(redis_module_ctx_t* ctx, latte_object_t* key) {
    redis_client_t* c = module_get_reply_client(ctx);
    redis_server_t* server = (redis_server_t*)c->client.server;
    const char* type_name = object_manager_get_type_name((uint8_t)key->type);
    if (!type_name || strcmp(type_name, "string") != 0) return NULL;
    size_t len = string_object_len(key);
    sds key_sds = sds_new_len(key->ptr, len);
    redis_db_t* db = server->dbs + c->dbid;
    if (expire_if_needed(server, db, key_sds)) {
        sds_delete(key_sds);
        return NULL;
    }
    dict_entry_t* ret = kv_store_dict_find(db->keys, get_kv_store_index_for_key(key_sds), key_sds);
    sds_delete(key_sds);
    return ret;
}

void redis_module_use_object_incr_count(latte_object_t* o) {
    latte_object_incr_ref_count(o);
}

void redis_module_use_object_decr_count(latte_object_t* o) {
    latte_object_decr_ref_count(o);
}

int redis_module_use_db_add(redis_module_ctx_t* ctx, latte_object_t* key, latte_object_t* val) {
    redis_client_t* c = module_get_reply_client(ctx);
    redis_server_t* server =  (redis_server_t*)c->client.server;
    redis_db_t* db = server->dbs + c->dbid;
    return db_add_key_value(server, db, key, val);
}

latte_object_t* redis_module_use_db_entry_get_value(dict_entry_t* de) {
    return dict_get_entry_val(de);
}

latte_object_t* redis_module_use_db_entry_set_value(dict_entry_t* de, latte_object_t* value) {
    latte_object_t* old = dict_get_entry_val(de);
    latte_object_decr_ref_count(old);
    dict_get_entry_val(de) = value;
}

int redis_module_use_object_is_string(latte_object_t* o) {
    if (!o) return 0;
    const char* type_name = object_manager_get_type_name((uint8_t)o->type);
    return (type_name && strcmp(type_name, "string") == 0);
}

void redis_module_use_reply_with_wrong_type_error(redis_module_ctx_t* ctx)  {
    redis_client_t* c = module_get_reply_client(ctx);
    add_reply(c, shared.wrongtypeerr);
}

void redis_module_use_reply_with_null(redis_module_ctx_t* ctx)  {
    struct redis_client_t* c = module_get_reply_client(ctx);
    add_reply_proto(c, "*-1\r\n", 5);
}

void redis_module_use_reply_with_object(redis_module_ctx_t* ctx, latte_object_t* o)  {
    redis_client_t* c = module_get_reply_client(ctx);
    add_reply_bulk(c, o);
}

/* --------------------------------------------------------------------------
 * ## Module information and time measurement
 * -------------------------------------------------------------------------- */

/**
 * 设置模块属性（内部初始化函数）
 * 功能: 由 RM_Init() 调用来设置 ctx->module 结构，这是内部函数，模块开发者无需直接使用
 * 输入: ctx - 模块上下文, name - 模块名, ver - 版本号, apiver - API版本号
 * 返回: 无
 */
void redis_module_use_set_module_attribs(redis_module_ctx_t *ctx, const char *name, int ver, int apiver) {
    /* Called by RM_Init() to setup the `ctx->module` structure.
     *
     * This is an internal function, Redis modules developers don't need
     * to use it. */
    redis_module_t *module;

    if (ctx->module != NULL) return;
    module = zmalloc(sizeof(*module));
    module->name = sds_new((char*)name);
    module->ver = ver;
    module->apiver = apiver;
    module->types = list_new();
    module->usedby = list_new();
    module->using = list_new();
    module->filters = list_new();
    module->in_call = 0;
    module->in_hook = 0;
    module->options = 0;
    module->info_cb = 0;
    module->defrag_cb = 0;
    ctx->module = module;
}


/**
 * 批量注册核心 API 函数
 * 功能: 注册 malloc/free/create_command 等核心 API 供模块使用
 * 输入: server - Redis服务器实例
 * 返回: 无
 */
void module_register_core_api(redis_server_t* server) {
    server->module_api = dict_new(&module_api_dict_type);
    REGISTER_API(server, malloc);
    REGISTER_API(server, calloc);
    REGISTER_API(server, realloc);
    REGISTER_API(server, free);
    REGISTER_API(server, create_command);
    REGISTER_API(server, is_module_name_busy);
    REGISTER_API(server, get_api);
    REGISTER_API(server, set_module_attribs);
    REGISTER_API(server, reply_with_simple_string);
    REGISTER_API(server, lookup_key);
    REGISTER_API(server, object_incr_count);
    REGISTER_API(server, object_decr_count);
    REGISTER_API(server, db_add);
    REGISTER_API(server, db_entry_get_value);
    REGISTER_API(server, db_entry_set_value);
    REGISTER_API(server, object_is_string);
    REGISTER_API(server, reply_with_wrong_type_error);
    REGISTER_API(server, reply_with_null);
    REGISTER_API(server, reply_with_object);
}


/* 模块条目管理 */

/**
 * 创建模块条目
 * 输入: path - 模块路径, argc - 参数数量, args - 参数数组
 * 返回: 新创建的模块条目指针
 */
module_entry_t* module_entry_new(sds path, int argc, char** args) {
    module_entry_t* entry = zmalloc(sizeof(module_entry_t));
    entry->path = path;
    entry->args = vector_new();
    for (int i = 0; i < argc; i++) {
        vector_push(entry->args, sds_new(args[i]));
    }
    return entry;
}

/**
 * 释放模块条目及其资源
 * 输入: data - 模块条目指针
 * 返回: 无
 */
void module_entry_delete(void* data) {
    module_entry_t* entry = (module_entry_t*)data;
    sds_delete(entry->path);
    while (vector_size(entry->args) > 0) {
        sds_delete(vector_pop(entry->args));
    }
    vector_delete(entry->args);
    zfree(entry);
}

/**
 * 从配置文件加载所有模块
 * 输入: server - Redis服务器实例
 * 返回: 成功返回1
 */
int init_redis_modules(redis_server_t* server) {
    if (server->config->load_modules == NULL) {
        LATTE_LIB_LOG(LOG_WARN, "no modules to load");
        return 1;
    }
    vector_t* entrys = server->config->load_modules;
    for (int i = 0; i < vector_size(entrys); i++) {
        module_entry_t* entry = vector_get(entrys, i);
        latte_assert_with_info(module_load(server, entry->path, (void**)entry->args, vector_size(entry->args)) == 0, "module load %s failed", entry->path);
    }
    return 1;
}
