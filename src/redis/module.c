


#include "module.h"
#include <dlfcn.h>
#include <sys/stat.h>
#include <string.h>
#define REDISMODULE_CORE 1
#include "redis_module.h"
#include "object/string.h"
#include "debug/latte_debug.h"
#include "../shared/shared.h"

#define REDIS_MODULE_CTX_AUTO_MEMORY (1<<0)
#define REDIS_MODULE_CTX_KEYS_POS_REQUEST (1<<1)
#define REDIS_MODULE_CTX_BLOCKED_REPLY (1<<2)
#define REDIS_MODULE_CTX_BLOCKED_TIMEOUT (1<<3)
#define REDIS_MODULE_CTX_THREAD_SAFE (1<<4)
#define REDIS_MODULE_CTX_BLOCKED_DISCONNECTED (1<<5)
#define REDIS_MODULE_CTX_MODULE_COMMAND_CALL (1<<6)
#define REDIS_MODULE_CTX_MULTI_EMITTED (1<<7)

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
int redis_module_use_get_api(redis_module_ctx_t* ctx, const char* funcname, void **ptr) {
    dict_entry_t* he = dict_find(ctx->server->module_api, funcname);
    if (!he) return -1;
    *ptr = dict_get_entry_val(he);
    return 0;
}

#define REDIS_MODULE_CTX_INIT {(void*)(unsigned long)&redis_module_use_get_api,NULL,NULL,0}
int module_load(redis_server_t* server,const char *path, void **module_argv, int module_argc) {
    int (*onload)(void *, void **, int);
    void* handle;
    redis_module_ctx_t ctx = REDIS_MODULE_CTX_INIT;
    ctx.server = server;
    LATTE_LIB_LOG(LL_WARN, "module load set server module_api %p", server->module_api);
    // ctx.client = module_free_context_reused_client;
    // selectDb(ctx.client, 0);

    struct stat st;
    // 检查文件是否存在
    if (stat(path, &st) == 0) {
        if (!(st.st_mode & (S_IXUSR  | S_IXGRP | S_IXOTH))) {
            LATTE_LIB_LOG(LL_WARN, "Module %s failed to load: It does not have execute permissions.", path);
            return -1;
        }
    }
    //动态打开so
    handle = dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if (handle == NULL) {
        LATTE_LIB_LOG(LL_WARN, "Module %s failed to load: %s", path, dlerror());
        return -1;
    }
    //获得执行函数 redis_module_onload
    onload = (int (*)(void *, void **, int))(unsigned long) dlsym(handle,"redis_module_onload");
    if (onload == NULL) {
        dlclose(handle);
        LATTE_LIB_LOG(LL_WARN, 
            "Module %s does not export RedisModule_OnLoad() "
            "symbol. Module not loaded.",path);
        return -1;
    }
    //加载失败
    if (onload((void*)&ctx, module_argv, module_argc) == -1) {
        if (ctx.module) {
            // module_unregister_commands(ctx.module);
            // module_unregister_shared_api(ctx.module);
            // module_unregister_used_api(ctx.module);
            // module_free_module_structure(ctx.module);
        }
        dlclose(handle);
        LATTE_LIB_LOG(LL_WARN, "Module %s initialization failed. Module not loaded",path);
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

void module_unload_command(redis_client_t* c) {

}

void module_list_command(redis_client_t* c) {

}


/** */
int module_register_api(redis_server_t* server ,const char *funname, void *funcptr) {
    return dict_add(server->module_api, (char*)funname, funcptr);
}

#define REGISTER_API(server, name) \
    module_register_api(server, "redis_module_" #name, (void *)(unsigned long)redis_module_use_ ## name)



/* server.moduleapi dictionary type. Only uses plain C strings since
 * this gets queries from modules. */

uint64_t dict_cstring_key_hash(const void *key) {
    return dict_gen_hash_function((unsigned char*)key, strlen((char*)key));
}

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


void* redis_module_use_malloc(size_t bytes) {
    return zmalloc(bytes);
}

void* redis_module_use_calloc(size_t nmemb, size_t size) {
    return zcalloc(nmemb*size);
}

void* redis_module_use_realloc(void* ptr, size_t bytes) {
    return zrealloc(ptr, bytes);
}

void redis_module_use_free(void* ptr) {
    zfree(ptr);
}

void module_free_context(redis_module_ctx_t* ctx) {

}

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

/* Return non-zero if the module name is busy.
 * Otherwise zero is returned. */
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
    redis_server_t* server =  (redis_server_t* )c->client.server;
    sds key_ptr;
    latte_assert(get_sds_from_object(key, &key_ptr) == 0);// "key is not a string"
    redis_db_t* db = server->dbs + c->dbid;
    return kv_store_dict_find(db->keys, get_kv_store_index_for_key(key_ptr) , key_ptr);
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
    return o->type == OBJ_STRING;
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
    add_reply(c, o);
}

/* --------------------------------------------------------------------------
 * ## Module information and time measurement
 * -------------------------------------------------------------------------- */

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