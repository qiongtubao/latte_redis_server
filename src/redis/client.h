
#ifndef __REDIS_CLIENT_H
#define __REDIS_CLIENT_H

#include "server/client.h"
#include "command.h"
#include "list/list.h"

/* 协议类型定义 */
#define PROTO_REQ_INLINE 1      /* 内联协议：简单的单行命令格式 */
#define PROTO_REQ_MULTIBULK 2   /* RESP多批量协议：Redis标准协议格式 */
/* 客户端状态标志位 */
#define CLIENT_SLAVE (1<<0)   /* 此客户端是从节点 */
#define CLIENT_MASTER (1<<1)  /* 此客户端是主节点 */
#define CLIENT_MONITOR (1<<2) /* 此客户端是监控器，参见 MONITOR 命令 */
#define CLIENT_MULTI (1<<3)   /* 此客户端正在事务上下文中 */
#define CLIENT_BLOCKED (1<<4) /* 客户端正在等待阻塞操作 */
#define CLIENT_DIRTY_CAS (1<<5) /* 监视的键被修改，EXEC 将失败 */
// #define CLIENT_CLOSE_AFTER_REPLY (1<<6) /* 回复发送完毕后关闭连接 */
#define CLIENT_UNBLOCKED (1<<7) /* 此客户端已解除阻塞并存储在
                                  server.unblocked_clients 列表中 */
#define CLIENT_LUA (1<<8) /* 这是 Lua 脚本使用的非连接客户端 */
#define CLIENT_ASKING (1<<9)     /* 客户端发出了 ASKING 命令 */
// #define CLIENT_CLOSE_ASAP (1<<10)/* 尽快关闭此客户端 */
#define CLIENT_UNIX_SOCKET (1<<11) /* 通过 Unix 域套接字连接的客户端 */
#define CLIENT_DIRTY_EXEC (1<<12)  /* 队列命令时出错，EXEC 将失败 */
#define CLIENT_MASTER_FORCE_REPLY (1<<13)  /* 即使是主节点也要排队回复 */
#define CLIENT_FORCE_AOF (1<<14)   /* 强制当前命令进行 AOF 传播 */
#define CLIENT_FORCE_REPL (1<<15)  /* 强制当前命令进行复制 */
#define CLIENT_PRE_PSYNC (1<<16)   /* 实例不理解 PSYNC 协议 */
#define CLIENT_READONLY (1<<17)    /* 集群客户端处于只读状态 */
#define CLIENT_PUBSUB (1<<18)      /* 客户端处于发布/订阅模式 */
#define CLIENT_PREVENT_AOF_PROP (1<<19)  /* 不传播到 AOF 文件 */
#define CLIENT_PREVENT_REPL_PROP (1<<20)  /* 不传播到从节点 */
#define CLIENT_PREVENT_PROP (CLIENT_PREVENT_AOF_PROP|CLIENT_PREVENT_REPL_PROP) /* 不进行任何传播 */
#define CLIENT_PENDING_WRITE (1<<21) /* 客户端有数据要发送但写操作待定 */
            


/* 客户端类型分类，用于客户端限制，目前仅用于
 * max-client-output-buffer 限制实现 */
#define CLIENT_TYPE_NORMAL 0 /* 普通请求-回复客户端 + 监控器 */
#define CLIENT_TYPE_SLAVE 1  /* 从节点 */
#define CLIENT_TYPE_PUBSUB 2 /* 订阅发布/订阅频道的客户端 */
#define CLIENT_TYPE_MASTER 3 /* 主节点 */
#define CLIENT_TYPE_COUNT 4  /* 客户端类型总数 */
#define CLIENT_TYPE_OBUF_COUNT 3 /* 暴露给输出的客户端数量 */
 

#define PROTO_INLINE_MAX_SIZE   (1024*64) /* 内联读取的最大大小 */

/** Redis 客户端结构体 */
typedef struct redis_client_t {
    latte_client_t client;              /* 基础客户端结构 */
    int flag;                          /* 客户端状态标志位（见 CLIENT_* 宏定义） */
    int req_type;                      /* 请求协议类型：PROTO_REQ_INLINE 或 PROTO_REQ_MULTIBULK */
    long long read_reploff;            /* 从主节点读取的复制偏移量 */
    int argc;                          /* 当前命令的参数数量 */
    latte_object_t** argv;             /* 当前命令的参数数组 */
    redis_command_t* cmd;              /* 当前正在执行的命令 */
    redis_command_t* lastcmd;          /* 上一个执行的命令 */
    size_t argv_len_sum;               /* 所有参数长度的总和 */
    long bulk_len;                     /* 当前 bulk 字符串的长度（-1 表示未知） */
    int multi_bulk_len;                /* multibulk 中剩余的元素数量 */
    unsigned long long reply_bytes;     /* 回复缓冲区中的字节数 */
    sds pending_querybuf;              /* 待处理的查询缓冲区 */
    long long repl_ack_time;           /* 复制确认时间戳 */
    int dbid;                          /* 当前选择的数据库 ID */

    /* 性能监控字段 */
    ustime_duration_t current_decode_time;  /* 当前解码耗时 */
    ustime_duration_t current_encode_time;  /* 当前编码耗时 */
    ustime_duration_t current_call_time;    /* 当前调用耗时 */
} redis_client_t;

/** 创建 Redis 客户端
 * 返回: 新创建的客户端实例
 */
latte_client_t* create_redis_client();

/** 获取客户端类型
 * 输入: c - Redis 客户端指针
 * 返回: 客户端类型（CLIENT_TYPE_NORMAL/SLAVE/PUBSUB/MASTER）
 */
int get_client_type(redis_client_t *c);

/** 删除 Redis 客户端（是否合并 redis_client_delete 和 free_redis_client）
 * 输入: client - 要删除的客户端
 */
void redis_client_delete(latte_client_t* client);

/** 释放 Redis 客户端内存
 * 输入: client - 要释放的 Redis 客户端
 */
void free_redis_client(redis_client_t* client);

/** 异步释放 Redis 客户端
 * 输入: client - 要异步释放的 Redis 客户端
 */
void free_redis_client_async(redis_client_t* client);

/** 发送对象回复给客户端
 * 输入: c - 客户端, o - 要发送的对象
 */
void add_reply(redis_client_t* c, latte_object_t* o);

/** 发送错误回复给客户端
 * 输入: c - 客户端, err - 错误消息字符串
 */
void add_reply_error(redis_client_t *c, const char *err);

/** 发送指定长度的错误回复
 * 输入: rc - 客户端, s - 错误消息, len - 消息长度
 */
void add_reply_error_length(redis_client_t* rc, const char *s, size_t len);

/** 发送格式化错误回复
 * 输入: c - 客户端, fmt - 格式字符串, ... - 可变参数
 */
void add_reply_error_format(redis_client_t *c, const char *fmt, ...);

/** 发送 bulk 格式回复
 * 输入: c - 客户端, obj - 要发送的对象
 */
void add_reply_bulk(redis_client_t* c, latte_object_t* obj);

/** 发送带前缀的长整数回复（用于 *N 和 $N 格式）
 * 输入: c - 客户端, ll - 长整数值, prefix - 前缀字符
 */
void add_reply_long_long_with_prefix(redis_client_t *c, long long ll, char prefix);

/** 发送帮助信息回复
 * 输入: c - 客户端, help - 帮助信息字符串数组
 */
void add_reply_help(redis_client_t* c, char** help);

#endif
