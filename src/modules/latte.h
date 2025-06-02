
#ifndef __REDIS_MODULES_LATTE_H
#define __REDIS_MODULES_LATTE_H


#include "../redis/redis_module.h"
#define UNUSED(x) ((void)(x))

int redis_module_onload(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc);
int init_string_module(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc);
// int init_hash_module(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc);





#endif