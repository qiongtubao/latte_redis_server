

#ifndef __REDIS_MODULE_H
#define __REDIS_MODULE_H

#include <stdint.h>
#include <stdio.h>

//公用模块
struct redis_module_ctx_t;

#define REDIS_MODULE_GET_API(ctx, name) \
    redis_module_get_api(ctx, "redis_module_" #name, ((void **)&redis_module_ ## name))



/* Default API declaration prefix (not 'extern' for backwards compatibility) */
#ifndef REDISMODULE_API
#define REDISMODULE_API
#endif

/* Macro definitions specific to individual compilers */
#ifndef REDISMODULE_ATTR_UNUSED
#    ifdef __GNUC__
#        define REDISMODULE_ATTR_UNUSED __attribute__((unused))
#    else
#        define REDISMODULE_ATTR_UNUSED
#    endif
#endif

#ifndef REDISMODULE_ATTR_COMMON
#    if defined(__GNUC__) && !defined(__clang__)
#        define REDISMODULE_ATTR_COMMON __attribute__((__common__))
#    else
#        define REDISMODULE_ATTR_COMMON
#    endif
#endif

/* Default API declaration suffix (compiler attributes) */
#ifndef REDISMODULE_ATTR
#define REDISMODULE_ATTR REDISMODULE_ATTR_COMMON
#endif

//module use
#ifndef REDISMODULE_CORE

typedef struct redis_module_ctx_t redis_module_ctx_t;
typedef struct redis_module_string_t redis_module_string_t;
typedef struct redis_module_get_swaps_result_t redis_module_get_swaps_result_t;


typedef void (*redis_module_get_swaps_func) (redis_module_ctx_t *ctx, redis_module_string_t **argv, int argc, redis_module_get_swaps_result_t *result);
typedef int (*redis_module_cmd_func)(redis_module_ctx_t *ctx, redis_module_string_t **argv, int argc);
REDISMODULE_API int (*redis_module_get_api)(redis_module_ctx_t*, const char *, void *) REDISMODULE_ATTR;
REDISMODULE_API int (*redis_module_create_command)(redis_module_ctx_t *ctx, const char *name, redis_module_cmd_func cmdfunc, redis_module_get_swaps_func getswapsfunc, const char *strflags, int firstkey, int lastkey, int keystep) REDISMODULE_ATTR;
REDISMODULE_API int (*redis_module_is_module_name_busy)(redis_module_ctx_t* ctx, const char *name) REDISMODULE_ATTR;
REDISMODULE_API void (*redis_module_set_module_attribs)(redis_module_ctx_t *ctx, const char *name, int ver, int apiver) REDISMODULE_ATTR;
REDISMODULE_API int (*redis_module_reply_with_simple_string)(redis_module_ctx_t *ctx, const char *msg) REDISMODULE_ATTR;


static int redis_module_init(redis_module_ctx_t *ctx, const char *name, int ver, int apiver) REDISMODULE_ATTR_UNUSED;
static int redis_module_init(redis_module_ctx_t *ctx, const char *name, int ver, int apiver) {
    void *getapifuncptr = ((void**)ctx)[0];
    redis_module_get_api = (int (*)(redis_module_ctx_t*,const char *, void *)) (unsigned long)getapifuncptr;
    REDIS_MODULE_GET_API(ctx, create_command);
    REDIS_MODULE_GET_API(ctx, is_module_name_busy);
    REDIS_MODULE_GET_API(ctx, get_api);
    REDIS_MODULE_GET_API(ctx, set_module_attribs);
    REDIS_MODULE_GET_API(ctx, reply_with_simple_string);
    #ifdef REDISMODULE_EXPERIMENTAL_API
    #endif

    if (redis_module_is_module_name_busy && redis_module_is_module_name_busy(ctx, name)) return -1;
    redis_module_set_module_attribs(ctx,name,ver,apiver);
    return 0;
}   

#else 
    
#define redis_module_string_t latte_object_t
#define redis_module_get_swaps_result_t get_key_requests_result_t
#endif



#endif