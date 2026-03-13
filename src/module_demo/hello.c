
#include "../redis/redis_module.h"

/**
 * hello.test 命令实现
 * 功能: 打印 hello 并返回 OK
 * 输入: ctx - redis上下文, argv - 命令参数, argc - 参数个数
 * 返回: 0-成功
 */
int hello_test_redis_command(redis_module_ctx_t* ctx, redis_module_string_t **argv, int argc) {
    printf("hello test\n");  // 打印hello测试信息
    redis_module_reply_with_simple_string(ctx, "OK");  // 返回OK响应
    return 0;
}

/**
 * hello 模块入口
 * 功能: 注册 hello.test 命令
 * 输入: ctx - redis模块上下文, argv - 参数数组, argc - 参数个数
 * 返回: 0-成功, -1-失败
 */
int redis_module_onload(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {

    if (redis_module_init(ctx, "hello", 1, 1) == -1) return -1;  // 初始化hello模块
    /* 注册hello.test命令，标记为只读操作 */
    if (redis_module_create_command(ctx, "hello.test", hello_test_redis_command, NULL, "readonly",0,0,0) == -1) {
        return -1;
    }
    return 0;
}