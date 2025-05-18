

#include "../redis/redis_module.h"


int hello_test_redis_command(redis_module_ctx_t* ctx, redis_module_string_t **argv, int argc) {
    printf("hello test\n");
    redis_module_reply_with_simple_string(ctx, "OK");
    return 0;
}
 
int redis_module_onload(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {
    
    if (redis_module_init(ctx, "hello", 1, 1) == -1) return -1;
    if (redis_module_create_command(ctx, "hello.test", hello_test_redis_command, NULL, "readonly",0,0,0) == -1) {
        return -1;
    }
    return 0;
}