#include "slowlog.h"
#include "../object/string.h"
#include "object/object_manager.h"
#include <limits.h>

#define SLOWLOG_ENTRY_MAX_ARGC 32       /* 慢查询条目最大参数数量 */
#define SLOWLOG_ENTRY_MAX_STRING 128    /* 慢查询条目单个参数字符串最大长度 */
#define OBJ_SHARED_REFCOUNT INT_MAX

/**
 * 复制字符串对象，用于 slowlog 记录，避免与数据库中的共享对象产生竞态条件
 * 输入: obj - 待复制的字符串对象
 * 返回: 新的字符串对象指针，失败返回 NULL
 */
static latte_object_t* dup_string_object(latte_object_t* obj) {
    if (!obj || !sds_encoded_object(obj)) {
        return NULL;
    }
    sds s = (sds)obj->ptr;
    sds dup_s = sds_dup(s);     /* 复制字符串内容 */
    if (!dup_s) {
        return NULL;
    }
    latte_object_t* new_obj = latte_object_string_new(dup_s);
    if (!new_obj) {
        sds_delete(dup_s);      /* 创建对象失败时清理内存 */
    }
    return new_obj;
}

/**
 * 创建慢查询条目，记录命令执行信息
 * 输入: id - 条目唯一ID, client - Redis客户端指针
 * 返回: 新创建的慢查询条目指针
 */
static slowlog_entry_t* slowlog_entry_new(long long id,
    redis_client_t* client) {
    slowlog_entry_t* entry = zmalloc(sizeof(slowlog_entry_t));
    entry->id = id;
    entry->time = ustime();
    entry->all_duration = client->client.end_time - client->client.start_time;
    entry->decode_duration = client->current_decode_time;
    entry->encode_duration = client->current_encode_time;
    entry->call_duration = client->current_call_time;
    entry->command = sds_new(client->lastcmd->name);

    /* 参数数量截断：超过最大数量时只保留前面部分 */
    int argc = client->argc;
    if (argc > SLOWLOG_ENTRY_MAX_ARGC) {
        argc = SLOWLOG_ENTRY_MAX_ARGC;
    }
    entry->argv = zmalloc(sizeof(latte_object_t*) * argc);
    for (int j = 0; j < argc; j++) {
        /* 如果参数被截断，最后一个参数显示省略信息 */
        if (client->argc != argc && j == argc-1) {
            entry->argv[j] = latte_object_string_new(
                sds_cat_printf(sds_empty(),"... (%d more arguments)",
                argc-argc+1));
        } else {
            /* 字符串截断：过长的字符串参数只保留前面部分 */
            if (sds_encoded_object(client->argv[j]) &&
                sds_len(client->argv[j]->ptr) > SLOWLOG_ENTRY_MAX_STRING)
            {
                sds s = sds_new_len(client->argv[j]->ptr, SLOWLOG_ENTRY_MAX_STRING);

                s = sds_cat_printf(s,"... (%lu more bytes)",
                    (unsigned long)
                    sds_len(client->argv[j]->ptr) - SLOWLOG_ENTRY_MAX_STRING);
                entry->argv[j] = latte_object_string_new(s);
            } else if (client->argv[j]->refcount == OBJ_SHARED_REFCOUNT) {
                /* 共享对象直接引用 */
                entry->argv[j] = client->argv[j];
            } else {
                /* Here we need to duplicate the string objects composing the
                 * argument vector of the command, because those may otherwise
                 * end shared with string objects stored into keys. Having
                 * shared objects between any part of Redis, and the data
                 * structure holding the data, is a problem: FLUSHALL ASYNC
                 * may release the shared string object and create a race. */
                /* 普通对象需要复制，避免与数据库中的对象共享造成竞态条件 */
                entry->argv[j] = dup_string_object(client->argv[j]);
            }
        }
    }
    entry->argc = argc;
    entry->client_name = client_get_name((latte_client_t*)client) ? sds_dup(client_get_name((latte_client_t*)client)) : NULL;
        entry->client_ip = sds_new(client_get_peer_id((latte_client_t*)client));
    return entry;
}

/**
 * 释放慢查询条目
 * 输入: entry - 慢查询条目指针
 * 返回: 无
 */
void slowlog_entry_delete(slowlog_entry_t* entry) {
    sds_delete(entry->client_ip);
    sds_delete(entry->client_name);
    /* 释放所有参数对象 */
    for (int i = 0; i < entry->argc; i++) {
        latte_object_decr_ref_count(entry->argv[i]);
    }
    zfree(entry->argv);
    zfree(entry->command);
    zfree(entry);
}

/**
 * 创建慢查询管理器
 * 输入: max_len - 最大条目数量, time_limit_us - 时间阈值(微秒)
 * 返回: 慢查询管理器指针
 */
slowlog_manager_t* slowlog_manager_new(long long max_len, long long time_limit_us) {
    slowlog_manager_t* manager = zmalloc(sizeof(slowlog_manager_t));
    manager->entries = list_new();
    manager->max_len = max_len;
    manager->next_id = 0L;
    manager->time_limit_us = time_limit_us;
    /* 设置条目释放回调函数 */
    manager->entries->free = (void (*)(void*))slowlog_entry_delete;
    return manager;
}

/**
 * 释放慢查询管理器
 * 输入: manager - 慢查询管理器指针
 * 返回: 无
 */
void slowlog_manager_delete(slowlog_manager_t* manager) {
    list_delete(manager->entries);   /* 会自动调用每个条目的释放函数 */
    zfree(manager);
}

/**
 * 动态修改慢查询时间阈值
 * 输入: manager - 慢查询管理器, time_limit_us - 新的时间阈值(微秒)
 * 返回: 无
 */
void slowlog_manager_set_time_limit_us(slowlog_manager_t* manager, long long time_limit_us) {
    manager->time_limit_us = time_limit_us;
}

/**
 * 动态修改最大条目数量，如果当前条目超出限制则删除多余的
 * 输入: manager - 慢查询管理器, max_len - 新的最大条目数量
 * 返回: 无
 */
void slowlog_manager_set_max_len(slowlog_manager_t* manager, long long max_len) {
    manager->max_len = max_len;
    /* 如果当前条目数超过新的限制，删除最旧的条目 */
    while (list_length(manager->entries) > manager->max_len) {
        list_del_node(manager->entries, list_last(manager->entries));
    }
}

/**
 * 检查命令执行时间，如果超过阈值则添加到慢查询日志
 * 输入: manager - 慢查询管理器, client - Redis客户端
 * 返回: 无
 */
void slowlog_manager_push_if_needed(slowlog_manager_t* manager, redis_client_t* client) {
    /* 如果配置为负值，表示禁用慢查询日志 */
    if (manager->max_len < 0 || manager->time_limit_us < 0) {
        return;
    }
    /* 只有执行时间超过阈值的命令才会被记录 */
    if (client->client.end_time - client->client.start_time > manager->time_limit_us) {
        slowlog_entry_t* entry = slowlog_entry_new(
            manager->next_id++,
            client);
        list_add_node_tail(manager->entries, entry);
    }

    /* 保持条目数量不超过最大限制，删除最旧的条目 */
    while (list_length(manager->entries) > manager->max_len) {
        list_del_node(manager->entries, list_last(manager->entries));
    }
}