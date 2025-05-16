


#include "module.h"
#include <dlfcn.h>
#include <sys/stat.h>
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
int module_load(redis_server_t* server,const char *path, void **module_argv, int module_argc) {
    int (*onload)(void *, void **, int);
    void* handle;
    redis_module_ctx_t ctx = REDIS_MODULE_CTX_INIT;
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