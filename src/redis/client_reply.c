
#include "client.h"
#include "list/list.h"
#include <string.h>
#include "debug/latte_debug.h"
#include "server.h"
#include "utils/utils.h"
#include "../object/string.h"
#include "object/object_manager.h"
#include "../shared/shared.h"

/** 准备客户端写回复
 * 输入: c - Redis客户端指针
 * 返回: 0 表示成功
 * 功能: 准备客户端进行写操作（当前实现为空）
 */
int prepare_client_to_write(redis_client_t* c) {
    return 0;
}


sds cat_redis_client_info_string(sds s, redis_client_t *client) {
    return s;
}


/** 获取客户端输出缓冲区内存使用量
 * 输入: c - Redis客户端指针
 * 返回: 输出缓冲区占用的字节数
 * 功能: 计算客户端输出缓冲区的内存使用情况，用于限制检查
 * 注意: 此函数非常快，可以根据调用者需要多次调用
 */
unsigned long get_client_output_buffer_memory_usage(redis_client_t *c) {
    unsigned long list_item_size = sizeof(list_node_t) + sizeof(client_reply_block_t);
    return c->reply_bytes + (list_item_size*list_length(c->client.reply));
}
/** 检查客户端输出缓冲区限制
 * 输入: c - Redis客户端指针
 * 返回: 非零值表示客户端达到了软限制或硬限制，否则返回零
 * 功能: 检查客户端是否达到输出缓冲区软限制或硬限制，并作为副作用更新检查软限制所需的状态
 */
int check_client_output_buffer_limits(redis_client_t *c) {
    return 0;
    // int soft = 0, hard = 0, class;
    // unsigned long used_mem = get_client_output_buffer_memory_usage(c);

    // class = get_client_type(c);
    // /* 出于输出缓冲区限制的目的，主节点被当作普通客户端处理 */
    // if (class == CLIENT_TYPE_MASTER) class = CLIENT_TYPE_NORMAL;

    // redis_server_t* server = c->client.server;

    // if (server->client_obuf_limits[class].hard_limit_bytes &&
    //     used_mem >= server.client_obuf_limits[class].hard_limit_bytes)
    //     hard = 1;
    // if (server->client_obuf_limits[class].soft_limit_bytes &&
    //     used_mem >= server.client_obuf_limits[class].soft_limit_bytes)
    //     soft = 1;

    // /* 我们需要检查软限制是否在指定的秒数内连续达到 */
    // if (soft) {
    //     if (c->obuf_soft_limit_reached_time == 0) {
    //         c->obuf_soft_limit_reached_time = server.unixtime;
    //         soft = 0; /* 第一次看到达到软限制 */
    //     } else {
    //         time_t elapsed = server.unixtime - c->obuf_soft_limit_reached_time;

    //         if (elapsed <=
    //             server.client_obuf_limits[class].soft_limit_seconds) {
    //             soft = 0; /* 客户端仍未达到软限制被认为达到的最大秒数 */
    //         }
    //     }
    // } else {
    //     c->obuf_soft_limit_reached_time = 0;
    // }
    // return soft || hard;
}




/** 在输出缓冲区限制达到时异步关闭客户端
 * 输入: c - Redis客户端指针, async - 是否异步关闭（1为异步，0为立即）
 * 返回: 1 表示客户端被（标记）关闭，0 表示未关闭
 * 功能: 当输出缓冲区达到软限制或硬限制时异步关闭客户端
 * 注意: 我们需要异步关闭客户端，因为此函数从无法安全释放客户端的上下文调用，
 *       即从将数据推入客户端输出缓冲区的较低级别函数调用
 */
int close_client_on_output_buffer_limit_reached(redis_client_t *c, int async) {
    if (!c->client.conn) return 0; /* 释放假客户端是不安全的 */
    latte_assert(c->reply_bytes < SIZE_MAX-(1024*64));
    if (c->reply_bytes == 0 || c->client.flags & CLIENT_CLOSE_ASAP) return 0;
    if (check_client_output_buffer_limits(c)) {
        sds client = cat_redis_client_info_string(sds_empty(),c);

        if (async) {
            free_redis_client_async(c);
            LATTE_LIB_LOG(LOG_WARN,
                      "Client %s scheduled to be closed ASAP for overcoming of output buffer limits.",
                      client);
        } else {
            free_redis_client(c);
            LATTE_LIB_LOG(LOG_WARN,
                      "Client %s closed for overcoming of output buffer limits.",
                      client);
        }
        sds_delete(client);
        return  1;
    }
    return 0;
}

/** 发送错误回复给客户端
 * 输入: c - Redis客户端指针, err - 错误消息字符串
 * 功能: 向客户端发送错误回复，参见 add_reply_error_length 的输入字符串期望
 */
void add_reply_error(redis_client_t *c, const char *err) {
    add_reply_error_length(c, err, strlen(err));
    // after_error_reply(c,err,strlen(err));
}

/** 发送指定长度的错误回复
 * 输入: c - Redis客户端指针, s - 错误消息, len - 消息长度
 * 功能: 向客户端发送指定长度的错误回复
 */
void add_reply_error_length(redis_client_t* c, const char *s, size_t len) {
    /* 如果字符串不是以 "-..." 开头，则错误代码由调用者提供。否则我们使用 "-ERR"。 */
    if (!len || s[0] != '-') add_reply_proto(c,"-ERR ",5);
    add_reply_proto(c,s,len);
    add_reply_proto(c,"\r\n",2);
}



/** 发送格式化的错误回复
 * 输入: c - Redis客户端指针, fmt - 格式字符串, ... - 可变参数
 * 功能: 发送格式化的错误回复，格式化字符串中可以安全地包含 \r 和 \n
 * 注意: 参见 add_reply_error_length 的格式化字符串期望
 */
void add_reply_error_format(redis_client_t *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sds_cat_vprintf(sds_empty(),fmt,ap);
    va_end(ap);
    /* 修剪末尾的任何换行符（add_reply_error_length 会添加） */
    s = sds_trim(s, "\r\n");
    /* 确保字符串中间没有换行符，否则会发出无效协议 */
    s = sds_map_chars(s, "\r\n", "  ",  2);
    add_reply_error_length(c,s,sds_len(s));
    // after_error_reply(c,s,sds_len(s));
    sds_delete(s);
}

/** 发送对象回复给客户端
 * 输入: c - Redis客户端指针, obj - 要发送的对象
 * 功能: 将 latte_object_t 对象作为回复发送给客户端
 */
void add_reply(redis_client_t* c, latte_object_t* obj) {
    if (prepare_client_to_write(c) != 0) return;
    if (!obj) {
        add_reply_error(c, "ERR Internal error: null object");
        return;
    }
    if (sds_encoded_object(obj)) {
        if (obj->ptr) {
            add_reply_proto(c, obj->ptr, sds_len((sds)obj->ptr));
        } else {
            add_reply_error(c, "ERR Internal error: null ptr in string object");
        }
    } else {
        /* 在新的 object_manager 系统中，所有字符串对象都是 sds 编码 */
        /* 如果对象不是字符串类型，尝试获取类型名并记录错误 */
        const char* type_name = object_manager_get_type_name((uint8_t)obj->type);
        if (type_name) {
            LATTE_LIB_LOG(LOG_ERROR, "add_reply: wrong object type '%s' (expected string)", type_name);
        } else {
            LATTE_LIB_LOG(LOG_ERROR, "add_reply: unknown object type %u", (unsigned)obj->type);
        }
        add_reply_error(c, "ERR Internal error: wrong object type");
    }
}

/** 发送带前缀的长整数回复（用于 *N 和 $N 格式）
 * 输入: c - Redis客户端指针, ll - 长整数值, prefix - 前缀字符
 * 功能: 输出 <prefix><long long><crlf> 格式的回复
 */
void add_reply_long_long_with_prefix(redis_client_t *c, long long ll, char prefix) {
    char buf[128];
    int len;

    /* 像 $3\r\n 或 *2\r\n 这样的内容在协议中经常出现，
     * 所以如果整数很小，我们有一些共享对象可以使用 */
    if (prefix == '*' && ll < OBJ_SHARED_BULKHDR_LEN && ll >= 0) {
        add_reply(c,shared.mbulkhdr[ll]);
        return;
    } else if (prefix == '$' && ll < OBJ_SHARED_BULKHDR_LEN && ll >= 0) {
        add_reply(c,shared.bulkhdr[ll]);
        return;
    }

    buf[0] = prefix;
    len = ll2string(buf+1,sizeof(buf)-1,ll);
    buf[len+1] = '\r';
    buf[len+2] = '\n';
    add_reply_proto(c,buf,len+3);
}


/** 创建 bulk 回复的长度前缀
 * 输入: c - Redis客户端指针, obj - 对象
 * 功能: 创建 bulk 回复的长度前缀，例如：$2234
 */
void add_reply_bulk_len(redis_client_t *c, latte_object_t *obj) {
    size_t len = string_object_len(obj);

    add_reply_long_long_with_prefix(c,len,'$');
}

/** 发送 bulk 格式回复
 * 输入: c - Redis客户端指针, obj - 要发送的对象
 * 功能: 发送完整的 bulk 回复：$<长度>\r\n<内容>\r\n
 */
void add_reply_bulk(redis_client_t* c, latte_object_t* obj) {
    add_reply_bulk_len(c,obj);
    add_reply(c,obj);
    add_reply(c,shared.crlf);
}


/** 裁剪回复缓冲区尾部的未使用空间
 * 输入: c - Redis客户端指针
 * 功能: 如果尾部块有大量未使用空间，则重新分配以节省内存
 */
void trim_reply_unused_tail_space(redis_client_t* c) {
    list_node_t* ln = list_last(c->client.reply);

    client_reply_block_t* tail = ln? list_node_value(ln): NULL;

    if (!tail) return;

    if (tail->size - tail->used > tail->size / 4 &&
        tail->used < PROTO_REPLY_CHUNK_BYTES) {
            size_t old_size = tail->size;
            tail = zrealloc(tail, tail->used + sizeof(client_reply_block_t));

        tail->size = zmalloc_usable_size(tail) - sizeof(client_reply_block_t);
        c->reply_bytes = c->reply_bytes + tail->size - old_size;
        list_node_value(ln) = tail;
    }
}

/** 添加延迟写入长度的节点
 * 输入: c - Redis客户端指针
 * 返回: 延迟长度节点的指针，可用于稍后设置实际长度
 * 功能: 为需要稍后填写长度的回复预留位置
 */
void* add_reply_deferred_len(redis_client_t* c) {
    if (prepare_client_to_write(c) != 0) return NULL;

    trim_reply_unused_tail_space(c);
    list_add_node_tail(c->client.reply, NULL);
    return list_last(c->client.reply);
}

/** 发送指定长度的状态回复
 * 输入: c - Redis客户端指针, s - 状态字符串, len - 字符串长度
 * 功能: 发送状态回复，格式为 +<状态>\r\n
 */
void add_reply_status_length(redis_client_t *c, const char *s, size_t len) {
    add_reply_proto(c,"+",1);
    add_reply_proto(c,s,len);
    add_reply_proto(c,"\r\n",2);
}

/** 发送格式化的状态回复
 * 输入: c - Redis客户端指针, fmt - 格式字符串, ... - 可变参数
 * 功能: 发送格式化的状态回复
 */
void add_reply_status_format(redis_client_t *c, const char *fmt, ...) {
    va_list ap;
    va_start(ap,fmt);
    sds s = sds_cat_vprintf(sds_empty(),fmt,ap);
    va_end(ap);
    add_reply_status_length(c,s,sds_len(s));
    sds_delete(s);
}

/** 发送状态回复
 * 输入: c - Redis客户端指针, status - 状态字符串
 * 功能: 发送状态回复
 */
void add_reply_status(redis_client_t *c, const char *status) {
    add_reply_status_length(c,status,strlen(status));
}

/** 设置延迟回复内容
 * 输入: c - Redis客户端指针, node - 延迟节点指针, s - 内容字符串, length - 内容长度
 * 功能: 为之前通过 add_reply_deferred_len 预留的节点设置实际内容
 *       这是一个复杂的优化函数，会尝试将内容合并到相邻节点以减少系统调用
 */
void set_deferred_reply(redis_client_t *c, void *node, const char *s, size_t length) {
    list_node_t *ln = (list_node_t*)node;
    client_reply_block_t *next, *prev;

    /* 当 *node 为 NULL 时中止：当客户端不应接受写入时，
     * 我们在 addReplyDeferredLen() 中返回 NULL */
    if (node == NULL) return;
    latte_assert(!list_node_value(ln));

    /* 通常我们填充这个由 addReplyDeferredLen() 添加的虚拟 NULL 节点，
     * 使用包含指定数组长度所需协议的新缓冲区结构。
     * 但是有时前一个/下一个节点中可能有空间，
     * 所以我们可以删除这个 NULL 节点，并在紧邻它之前/之后的节点中
     * 后缀/前缀我们的数据，以便稍后节省一个 write(2) 系统调用。
     * 需要满足的条件：
     *
     * - 前一个节点非 NULL 且有空间或
     * - 下一个节点非 NULL，
     * - 它有足够的已分配空间
     * - 且不太大（避免大的 memmove） */
    if (ln->prev != NULL && (prev = list_node_value(ln->prev)) &&
        prev->size - prev->used > 0)
    {
        size_t len_to_copy = prev->size - prev->used;
        if (len_to_copy > length)
            len_to_copy = length;
        memcpy(prev->buf + prev->used, s, len_to_copy);
        prev->used += len_to_copy;
        length -= len_to_copy;
        if (length == 0) {
            list_del_node(c->client.reply, ln);
            return;
        }
        s += len_to_copy;
    }

    if (ln->next != NULL && (next = list_node_value(ln->next)) &&
        next->size - next->used >= length &&
        next->used < PROTO_REPLY_CHUNK_BYTES * 4)
    {
        memmove(next->buf + length, next->buf, next->used);
        memcpy(next->buf, s, length);
        next->used += length;
        list_del_node(c->client.reply,ln);
    } else {
        /* 创建新节点 */
        client_reply_block_t *buf = zmalloc(length + sizeof(client_reply_block_t));
        /* 利用分配的内部碎片 */
        buf->size = zmalloc_usable_size(buf) - sizeof(client_reply_block_t);
        buf->used = length;
        memcpy(buf->buf, s, length);
        list_node_value(ln) = buf;
        c->reply_bytes += buf->size;

        // closeClientOnOutputBufferLimitReached(c, 1);
    }
}

/** 填充长度对象并尝试将其粘合到下一个块
 * 输入: c - Redis客户端指针, node - 延迟节点指针, length - 长度值, prefix - 前缀字符
 * 功能: 设置延迟的聚合长度（如数组长度或bulk长度）
 */
void set_deferred_aggregate_len(redis_client_t *c, void *node, long length, char prefix) {
    latte_assert(length >= 0);

    /* 当 *node 为 NULL 时中止：当客户端不应接受写入时，
     * 我们在 addReplyDeferredLen() 中返回 NULL */
    if (node == NULL) return;

    char lenstr[128];
    size_t lenstr_len = sprintf(lenstr, "%c%ld\r\n", prefix, length);
    set_deferred_reply(c, node, lenstr, lenstr_len);
}

/** 设置延迟的数组长度
 * 输入: c - Redis客户端指针, node - 延迟节点指针, length - 数组长度
 * 功能: 设置延迟的数组长度，格式为 *<length>\r\n
 */
void set_deferred_array_len(redis_client_t *c, void *node, long length) {
    set_deferred_aggregate_len(c,node,length,'*');
}

/** 发送帮助信息回复
 * 输入: c - Redis客户端指针, help - 帮助信息字符串数组
 * 功能: 发送命令的帮助信息，包括子命令列表和通用帮助
 */
void add_reply_help(redis_client_t* c, char** help) {
    sds cmd = sds_new((char*) c->argv[0]->ptr);

    void *blenp = add_reply_deferred_len(c);
    int blen = 0;

    sds_to_upper(cmd);  // 将命令名转为大写
    add_reply_status_format(c, "%s <subcommand> [<arg> [value] [opt] ...]. Subcommands are:", cmd);
    sds_delete(cmd);
    while (help[blen]) add_reply_status(c, help[blen++]);  // 添加每个帮助项

    add_reply_status(c, "HELP");
    add_reply_status(c, "   Prints this help.");

    blen += 1; /* 计入标题 */
    blen += 2; /* 计入页脚 */
    set_deferred_array_len(c, blenp, blen);  // 设置数组长度

}