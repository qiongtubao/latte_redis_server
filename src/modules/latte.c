

#include "latte.h"
 
int redis_module_onload(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {
    
    if (redis_module_init(ctx, "latte", 1, 1) == -1) return -1;
    if (init_string_module(ctx, argv, argc) == -1) {
        return -1;
    }
    return 0;
}