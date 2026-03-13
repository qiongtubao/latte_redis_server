

#include "latte.h"

/**
 * SET key value 命令实现
 * 功能: 查找 key，不存在则新增，存在则覆盖字符串值，类型不符返回 WRONGTYPE
 * 输入: ctx - redis上下文, argv - 命令参数, argc - 参数个数
 * 返回: 0-成功, -1-失败
 */
int set_command(redis_module_ctx_t* ctx, redis_module_string_t **argv, int argc) {
    if (argc != 3) {
        redis_module_reply_with_simple_string(ctx, "ERR wrong number of arguments for 'set' command");
        return -1;
    }
    redis_module_string_t* key = argv[1];  // 获取key参数
    redis_module_db_entry_t* o = redis_module_lookup_key(ctx, key);  // 查找key是否存在
    redis_module_object_t* value;
    if (o == NULL) {
        /* key不存在，创建新的键值对 */
        value = (redis_module_object_t*)argv[2];
        redis_module_object_incr_count(value);  // 增加引用计数
        redis_module_db_add(ctx, key, value);  // 添加到数据库
    } else {
        /* key存在，检查类型并更新值 */
        value = redis_module_db_entry_get_value(o);
        if (redis_module_object_is_string(value)) {
            value = (redis_module_object_t*)argv[2];
            redis_module_object_incr_count(value);  // 增加引用计数
            redis_module_db_entry_set_value(o, value);  // 设置新值
        } else {
            /* 类型不匹配，返回WRONGTYPE错误 */
            redis_module_reply_with_wrong_type_error(ctx);
            return -1;
        }
    }
    redis_module_reply_with_simple_string(ctx, "OK");
    return 0;
}


/**
 * GET key 命令实现
 * 功能: 查找 key，不存在返回 null，存在返回字符串值
 * 输入: ctx - redis上下文, argv - 命令参数, argc - 参数个数
 * 返回: 0-成功, -1-失败
 */
int get_command(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {
    if (argc != 2) {
        redis_module_reply_with_simple_string(ctx, "ERR wrong number of arguments for 'get' command");
        return -1;
    }
    redis_module_db_entry_t* o = redis_module_lookup_key(ctx, argv[1]);  // 查找key
    if (o == NULL) {
        /* key不存在，返回null */
        redis_module_reply_with_null(ctx);
    } else {
        redis_module_object_t* value = redis_module_db_entry_get_value(o);  // 获取值
        if (redis_module_object_is_string(value)) {
            /* 是字符串类型，返回值 */
            redis_module_reply_with_object(ctx, value);
        } else {
            /* 类型不匹配，返回WRONGTYPE错误 */
            redis_module_reply_with_wrong_type_error(ctx);
            return -1;
        }
    }
    return 0;
}

/**
 * 注册 set/get 命令到模块系统
 * 输入: ctx - redis模块上下文, argv - 参数数组, argc - 参数个数
 * 返回: 0-成功, -1-失败
 */
int init_string_module(redis_module_ctx_t* ctx, redis_module_string_t** argv, int argc) {
    UNUSED(argv);
    UNUSED(argc);
    /* 注册set命令，标记为写操作和拒绝内存不足 */
    if (redis_module_create_command(ctx, "set", set_command, NULL, "write deny-oom",0,0,0) == -1) {
        return -1;
    }
    /* 注册get命令，标记为只读操作 */
    if (redis_module_create_command(ctx, "get", get_command, NULL, "readonly",0,0,0) == -1) {
        return -1;
    }
    return 0;
}