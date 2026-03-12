/**
 * backlog 实现：长度限制、当前总长度统计
 */
#include "backlog.h"
#include "sds/sds.h"
#include "zmalloc/zmalloc.h"
#include <stdlib.h>

/**
 * sds 释放包装函数，用作 list 的 free 回调
 * 输入: ptr - sds 字符串指针
 * 返回: 无
 */
static void sds_free_wrapper(void* ptr) {
    if (ptr) sds_delete((sds)ptr);
}

/**
 * 创建 backlog 实例，设置容量限制
 * 输入: max_entries - 最大条目数, max_total_len - 最大总长度
 * 返回: backlog 实例指针，失败返回 NULL
 */
backlog_t* backlog_new(size_t max_entries, size_t max_total_len) {
    backlog_t* b = (backlog_t*)zmalloc(sizeof(backlog_t));
    if (!b) return NULL;
    b->entries = list_new();
    if (!b->entries) {
        zfree(b);
        return NULL;
    }
    /* 设置 list 的 free 方法为 sds_free_wrapper */
    list_set_free_method(b->entries, sds_free_wrapper);
    b->max_entries = max_entries;
    b->max_total_len = max_total_len;
    b->current_len = 0;
    return b;
}

/**
 * 释放 backlog 及所有条目
 * 输入: b - backlog 实例指针
 * 返回: 无
 */
void backlog_delete(backlog_t* b) {
    if (!b) return;
    if (b->entries) list_delete(b->entries);  /* list_delete 会调用 sds_free_wrapper 释放每个条目 */
    zfree(b);
}

/** 从头部移除一条并更新 current_len */
static void backlog_pop_head(backlog_t* b) {
    list_node_t* head = list_first(b->entries);
    if (!head) return;
    sds s = (sds)list_node_value(head);
    if (s) b->current_len -= sds_len(s);
    list_del_node(b->entries, head);
}

/**
 * 追加记录到 backlog，自动处理容量限制
 * 先按条数限制淘汰，再按总长度限制淘汰
 * 输入: b - backlog 实例, entry - 待添加的 sds 条目
 * 返回: 0 成功, -1 失败
 */
int backlog_add(backlog_t* b, sds entry) {
    if (!b || !b->entries || !entry) return -1;
    size_t entry_len = sds_len(entry);

    /* 条数限制：超出时从头部移除 */
    while (b->max_entries > 0 && list_length(b->entries) >= b->max_entries) {
        backlog_pop_head(b);
    }
    /* 总长度限制：加入后若超出则从头部移除直到可放入 */
    while (b->max_total_len > 0 &&
           list_length(b->entries) > 0 &&
           b->current_len + entry_len > b->max_total_len) {
        backlog_pop_head(b);
    }

    list_add_node_tail(b->entries, entry);
    b->current_len += entry_len;
    return 0;
}

/**
 * 查询 backlog 中的条目数量
 * 输入: b - backlog 实例
 * 返回: 条目数量
 */
size_t backlog_count(const backlog_t* b) {
    return b && b->entries ? (size_t)list_length(b->entries) : 0;
}

/**
 * 查询 backlog 中所有条目的总长度
 * 输入: b - backlog 实例
 * 返回: 总长度（字节）
 */
size_t backlog_total_len(const backlog_t* b) {
    return b ? b->current_len : 0;
}
