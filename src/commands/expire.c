#include "command_manager.h"
#include "../redis/db.h"
#include "../redis/server.h"
#include "../object/string.h"
#include "../shared/shared.h"
#include "utils/utils.h"
#include <stdio.h>

/**
 * EXPIRE命令实现：为指定键设置过期时间
 * 输入: c - 客户端连接对象
 * 用法: EXPIRE key seconds
 * 功能: 将key的过期时间设置为当前时间+seconds秒，使用毫秒时间戳存储
 * 返回: 1表示成功设置，0表示键不存在
 */
void expire_command(redis_client_t* c) {
    redis_server_t* server = (redis_server_t*)c->client.server;
    redis_db_t* db = server->dbs + c->dbid;  /* 获取当前客户端使用的数据库 */
    sds key_sds;
    long long seconds;

    /* 参数数量校验：必须是3个参数（EXPIRE key seconds） */
    if (c->argc != 3) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command", c->cmd->name);
        return;
    }

    /* 解析seconds参数：必须是非负整数 */
    if (string2ll(c->argv[2]->ptr, string_object_len(c->argv[2]), &seconds) == 0 || seconds < 0) {
        add_reply_error(c, "ERR value is not an integer or out of range");
        return;
    }

    /* 构造键的sds字符串用于查找 */
    key_sds = sds_new_len(c->argv[1]->ptr, string_object_len(c->argv[1]));

    /* 检查键是否存在：在相应分片中查找键 */
    if (kv_store_dict_find(db->keys, get_kv_store_index_for_key(key_sds), key_sds) == NULL) {
        sds_delete(key_sds);
        add_reply_long_long_with_prefix(c, 0, ':');  /* 键不存在，返回0 */
        return;
    }

    /* 设置过期时间：当前毫秒时间戳 + seconds*1000 */
    if (db_set_expire(server, db, key_sds, mstime() + seconds * 1000) != 0) {
        sds_delete(key_sds);
        add_reply_error(c, "ERR could not set expire");
        return;
    }

    sds_delete(key_sds);
    add_reply_long_long_with_prefix(c, 1, ':');  /* 成功设置过期时间，返回1 */
}
