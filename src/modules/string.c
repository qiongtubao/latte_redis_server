


#include "latte.h"


int set_command(redis_module_ctx_t* ctx, redis_module_string_t **argv, int argc) {
    UNUSED(argc);
    redis_module_string_t* key = argv[1];
    redis_module_db_entry_t* o = redis_module_lookup_key(ctx, key);
    redis_module_object_t* value;
    if (o == NULL) {
        value = (redis_module_object_t*)argv[2];
        redis_module_object_incr_count(value);
        redis_module_db_add(ctx, key, value);
    } else {
        value = redis_module_db_entry_get_value(o);
        if (redis_module_object_is_string(value)) {
            value = (redis_module_object_t*)argv[2];
            redis_module_object_incr_count(value);
            redis_module_db_entry_set_value(o, value);
        } else {
            redis_module_reply_with_wrong_type_error(ctx);
            return -1;
        }
    }
    redis_module_reply_with_simple_string(ctx, "OK");
    return 0;
}


int get_command(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {
    UNUSED(argc);
    redis_module_db_entry_t* o = redis_module_lookup_key(ctx, argv[1]);
    if (o == NULL) {
        redis_module_reply_with_null(ctx);
    } else {
        redis_module_object_t* value = redis_module_db_entry_get_value(o);
        if (redis_module_object_is_string(value)) {
            redis_module_reply_with_object(ctx, value);
        } else {
            redis_module_reply_with_wrong_type_error(ctx);
            return -1;
        }
    } 
    return 0;
}
int init_string_module(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    if (redis_module_create_command(ctx, "set", set_command, NULL, "write deny-oom",0,0,0) == -1) {
        return -1;
    }
    if (redis_module_create_command(ctx, "get", get_command, NULL, "readonly",0,0,0) == -1) {
        return -1;
    }
    return 0;
}