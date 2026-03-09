/**
 * backlog 实现：长度限制、当前总长度统计
 */
#include "backlog.h"
#include "sds/sds.h"
#include "zmalloc/zmalloc.h"
#include <stdlib.h>

static void sds_free_wrapper(void* ptr) {
    if (ptr) sds_delete((sds)ptr);
}

backlog_t* backlog_new(size_t max_entries, size_t max_total_len) {
    backlog_t* b = (backlog_t*)zmalloc(sizeof(backlog_t));
    if (!b) return NULL;
    b->entries = list_new();
    if (!b->entries) {
        zfree(b);
        return NULL;
    }
    list_set_free_method(b->entries, sds_free_wrapper);
    b->max_entries = max_entries;
    b->max_total_len = max_total_len;
    b->current_len = 0;
    return b;
}

void backlog_delete(backlog_t* b) {
    if (!b) return;
    if (b->entries) list_delete(b->entries);
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

size_t backlog_count(const backlog_t* b) {
    return b && b->entries ? (size_t)list_length(b->entries) : 0;
}

size_t backlog_total_len(const backlog_t* b) {
    return b ? b->current_len : 0;
}
