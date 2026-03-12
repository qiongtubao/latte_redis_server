

#ifndef __REDIS_SLOWLOG_H
#define __REDIS_SLOWLOG_H

#include "client.h"

typedef struct slowlog_entry_t {
    long long id;                    /* 慢查询条目的唯一标识符 */
    ustime_t time;                   /* 命令执行的时间戳 */
    long long all_duration;          /* 命令总耗时 (微秒) */
    long long decode_duration;       /* 协议解码耗时 (微秒) */
    long long encode_duration;       /* 响应编码耗时 (微秒) */
    long long call_duration;         /* 命令调用耗时 (微秒) */
    sds command;                     /* 执行的命令名称 */
    sds client_name;                 /* 客户端名称 */
    sds client_ip;                   /* 客户端IP地址 */
    latte_object_t **argv;           /* 命令参数数组 */
    int argc;                        /* 命令参数个数 */
} slowlog_entry_t;
// slowlog_entry_t* slowlog_entry_new(long long id, 
//     redis_client_t* client);
// void slowlog_entry_delete(slowlog_entry_t* entry);

typedef struct slowlog_manager_t {
    list_t* entries;                 /* 慢查询条目列表 */
    long long max_len;               /* 最大保存的慢查询条目数量 */
    long long time_limit_us;         /* 慢查询时间阈值 (微秒) */
    long long next_id;               /* 下一个条目的ID */
} slowlog_manager_t;

/**
 * 创建慢查询管理器
 * 输入: max_len - 最大条目数量, time_limit_us - 时间阈值(微秒)
 * 返回: 慢查询管理器指针
 */
slowlog_manager_t* slowlog_manager_new(long long max_len, long long time_limit_us);

/**
 * 释放慢查询管理器
 * 输入: manager - 慢查询管理器指针
 * 返回: 无
 */
void slowlog_manager_delete(slowlog_manager_t* manager);

/**
 * 如果命令执行时间超过阈值，则添加到慢查询日志
 * 输入: manager - 慢查询管理器, client - Redis客户端
 * 返回: 无
 */
void slowlog_manager_push_if_needed(slowlog_manager_t* manager, redis_client_t* client);



#endif