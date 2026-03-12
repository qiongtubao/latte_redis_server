
#ifndef __REDIS_COMMAND_H
#define __REDIS_COMMAND_H

#include <stdint.h>
#include "object/object.h"

/* 命令调用标志位，用于 call() 函数 */
#define CMD_CALL_NONE 0                           /* 无特殊标志 */
#define CMD_CALL_SLOWLOG (1<<0)                   /* 记录慢查询日志 */
#define CMD_CALL_STATS (1<<1)                     /* 统计命令执行信息 */
#define CMD_CALL_PROPAGATE_AOF (1<<2)             /* 传播到 AOF 文件 */
#define CMD_CALL_PROPAGATE_REPL (1<<3)            /* 传播到从服务器 */
#define CMD_CALL_PROPAGATE (CMD_CALL_PROPAGATE_AOF|CMD_CALL_PROPAGATE_REPL) /* 传播到 AOF 和从服务器 */
#define CMD_CALL_FULL (CMD_CALL_SLOWLOG | CMD_CALL_STATS | CMD_CALL_PROPAGATE) /* 启用全部功能 */
#define CMD_CALL_NOWRAP (1<<4)  /* 不包装传播数组到 MULTI/EXEC：调用者将处理 */
 
/* 命令属性标志位，详细含义请查看 server.c 文件中的命令表 */
#define CMD_WRITE (1ULL<<0)            /* 写入操作标志 */
#define CMD_READONLY (1ULL<<1)         /* 只读操作标志 */
#define CMD_DENYOOM (1ULL<<2)          /* 内存不足时拒绝执行标志 */
#define CMD_MODULE (1ULL<<3)           /* 模块导出的命令标志 */
#define CMD_ADMIN (1ULL<<4)            /* 管理员命令标志 */
#define CMD_PUBSUB (1ULL<<5)           /* 发布订阅命令标志 */
#define CMD_NOSCRIPT (1ULL<<6)         /* 不允许在脚本中使用标志 */
#define CMD_RANDOM (1ULL<<7)           /* 随机结果命令标志 */
#define CMD_SORT_FOR_SCRIPT (1ULL<<8)  /* 脚本排序标志 */
#define CMD_LOADING (1ULL<<9)          /* 加载期间可执行标志 */
#define CMD_STALE (1ULL<<10)           /* 允许过期数据执行标志 */
#define CMD_SKIP_MONITOR (1ULL<<11)    /* 跳过监控标志 */
#define CMD_SKIP_SLOWLOG (1ULL<<12)    /* 跳过慢查询日志标志 */
#define CMD_ASKING (1ULL<<13)          /* 集群询问标志 */
#define CMD_FAST (1ULL<<14)            /* 快速执行标志 */
#define CMD_NO_AUTH (1ULL<<15)         /* 无需认证标志 */
#define CMD_MAY_REPLICATE (1ULL<<16)   /* 可能复制标志 */
/* 模块系统使用的命令标志位 */
#define CMD_MODULE_GETKEYS (1ULL<<17)  /* 使用模块 getkeys 接口 */
#define CMD_MODULE_NO_CLUSTER (1ULL<<18) /* 集群环境下拒绝执行 */


typedef struct redis_client_t redis_client_t;
#define MAX_KEYS_BUFFER 256
/**
 * 获取键结果结构体
 * 用于存储命令解析出的键信息
 */
typedef struct {
    int keys_buf[MAX_KEYS_BUFFER];  /* 键索引缓冲区 */
    int *keys;                      /* 键索引数组指针 */
    int numkeys;                    /* 键的数量 */
    int size;                       /* 数组大小 */
} get_keys_result_t;
typedef void redis_command_proc_func(struct redis_client_t *c);
typedef int redis_get_keys_proc_func(struct redis_command_t *cmd, latte_object_t **argv, int argc, get_keys_result_t *result);
typedef int (*redis_get_key_requests_proc_func)(int dbid, struct redis_command_t *cmd, latte_object_t **argv, int argc, struct get_key_requests_result *result);
/**
 * Redis 命令结构体
 * 定义了一个命令的所有属性和统计信息
 */
typedef struct redis_command_t {
    char* name;                                   /* 命令名称 */
    redis_command_proc_func * proc;               /* 命令处理函数指针 */
    int arity;                                    /* 参数数量（负数表示最少参数数，正数表示精确参数数） */
    char * sflags;                                /* 命令标志字符串 */
    uint64_t flags;                               /* 命令标志位掩码 */

    redis_get_keys_proc_func* get_keys_proc;      /* 获取键的函数指针 */
    redis_get_key_requests_proc_func* get_key_requests_proc; /* 获取键请求的函数指针 */
    int intention;                                /* 命令意图 */
    uint32_t intention_flags;                     /* 意图标志位 */
    int firstkey;                                 /* 第一个键参数的位置 */
    int lastkey;                                  /* 最后一个键参数的位置 */
    int keystep;                                  /* 键参数的步长 */
    long long microseconds, calls, rejected_calls, failed_calls; /* 执行时间统计和调用计数 */
    int id;                                       /* 命令 ID */

} redis_command_t;




#endif