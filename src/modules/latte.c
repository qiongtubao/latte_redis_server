

#include "latte.h"

/**
 * 注册 latte 模块，依次初始化 string 子模块
 * 输入: ctx - redis模块上下文, argv - 参数数组, argc - 参数个数
 * 返回: 0-成功, -1-失败
 */
int redis_module_onload(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {

    if (redis_module_init(ctx, "latte", 1, 1) == -1) return -1;  // 初始化latte模块
    if (init_string_module(ctx, argv, argc) == -1) {  // 初始化string子模块
        return -1;
    }
    return 0;
}