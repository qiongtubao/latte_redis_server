/**
 * backlog - 命令执行记录，支持条数限制与总长度限制
 *
 * 每条记录为一个 sds（命令名 + 参数，空格分隔）。
 * max_entries / max_total_len 为 0 表示该维度不限制。
 */
#ifndef __REDIS_BACKLOG_H
#define __REDIS_BACKLOG_H

#include "list/list.h"
#include "sds/sds.h"
#include <stddef.h>

typedef struct backlog_t {
    list_t* entries;        /**< 条目列表，每项为 sds */
    size_t max_entries;     /**< 最大条数，0 表示不限制 */
    size_t max_total_len;  /**< 最大总字节数，0 表示不限制 */
    size_t current_len;    /**< 当前所有条目的总字节数 */
} backlog_t;

/** 创建 backlog，max_entries/max_total_len 为 0 表示不限制 */
backlog_t* backlog_new(size_t max_entries, size_t max_total_len);

/** 释放 backlog 及其所有条目 */
void backlog_delete(backlog_t* b);

/**
 * 追加一条记录；若超出限制则从头部移除旧记录后再追加。
 * entry 由 backlog 接管所有权，调用方不再 sds_delete(entry)。
 * 返回 0 成功，-1 失败（如 entry 为 NULL）。
 */
int backlog_add(backlog_t* b, sds entry);

/** 当前条目数量 */
size_t backlog_count(const backlog_t* b);

/** 当前总字节数 */
size_t backlog_total_len(const backlog_t* b);

#endif /* __REDIS_BACKLOG_H */
