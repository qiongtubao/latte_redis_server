
#ifndef __REDIS_MODULES_LATTE_H
#define __REDIS_MODULES_LATTE_H


#include "../redis/redis_module.h"
#define UNUSED(x) ((void)(x))

/**
 * redis模块入口函数
 * 输入: ctx - redis模块上下文, argv - 参数数组, argc - 参数个数
 * 返回: 0-成功, -1-失败
 */
int redis_module_onload(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc);

/**
 * string子模块初始化函数
 * 输入: ctx - redis模块上下文, argv - 参数数组, argc - 参数个数
 * 返回: 0-成功, -1-失败
 */
int init_string_module(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc);
// int init_hash_module(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc);





#endif