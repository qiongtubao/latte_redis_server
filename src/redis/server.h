#ifndef __REDIS_SERVER_H
#define __REDIS_SERVER_H
#include "server/server.h"
#include "../redis_config/config.h"
#include "../commands/command_manager.h"
#include "sds/sds.h"
#include "dict/dict.h"
#include "command.h"
#include "db.h"
#include "backlog.h"
#include "slowlog.h"
#include "../experiment/metric.h"
#include <stdint.h>

/* ========== 主从复制状态枚举 ========== */
typedef enum {
    REPL_STATE_NONE = 0,       /* 未开启复制（普通主节点状态） */
    REPL_STATE_CONNECT,        /* 需要连接到 master，等待触发 */
    REPL_STATE_CONNECTING,     /* TCP 连接中（非阻塞连接进行中） */
    REPL_STATE_SEND_PING,      /* 连接建立后，发送 PING 测试连接 */
    REPL_STATE_WAIT_PONG,      /* 等待 master 的 PONG 回复 */
    REPL_STATE_SEND_PSYNC,     /* 发送 PSYNC/FULLRESYNC 请求全量同步 */
    REPL_STATE_TRANSFER,       /* 正在接收 master 的全量数据（LDB 内容） */
    REPL_STATE_CONNECTED,      /* 全量同步完成，已连接，进行增量复制 */
} repl_state_t;

/* 服务器内存淘汰策略标志位。使用标志位集合而不是简单的增量数字，
 * 以便更快速地测试多个策略的公共属性。 */
#define MAXMEMORY_FLAG_LRU (1 << 0)                   // LRU (最近最少使用) 策略标志位
#define MAXMEMORY_FLAG_LFU (1 << 1)                   // LFU (最少频率使用) 策略标志位
#define MAXMEMORY_FLAG_ALLKEYS (1 << 2)               // 所有键策略标志位
#define MAXMEMORY_FLAG_NO_SHARED_INTEGERS (MAXMEMORY_FLAG_LRU | MAXMEMORY_FLAG_LFU)  // 不共享整数对象的策略组合

/** Redis服务器核心数据结构 */
typedef struct redis_server_t {
    latte_server_t server;                     // 底层服务器结构体
    int exec_argc;                             // 执行参数个数
    sds* exec_argv;                           // 执行参数数组
    sds executable;                           // 可执行文件路径
    sds configfile;                           // 配置文件路径
    config_manager_t* config_manager;         // 配置管理器
    server_config_t* config;                  // 服务器配置结构体
    command_manager_t* command_manager;       // 命令管理器
    dict_t* robj_register;                    // Redis对象注册器字典
    list_t* clients_to_close;                // 待关闭的客户端列表
    /** 命令执行后写入的 backlog（含长度限制与总长度统计） */
    backlog_t* backlog;                       // 命令执行回放缓冲区
    time_t unixtime;                          // Unix时间戳
    int hz;                                   // 服务器运行频率
    struct redis_client_t* current_client;   // 当前处理的客户端

    /** module */
    dict_t* modules;                          // 已加载模块字典
    dict_t* module_api;                       // 模块API接口字典

    /* config */
    long long proto_max_bulk_len;             // 协议最大块长度限制

    /* lru or lfu */
    int maxmemory_policy;                     // 内存淘汰策略
    unsigned int lruclock;                    // LRU时钟

    /** db */
    struct redis_db_t* dbs;                   // 数据库数组
    int db_num;                               // 数据库数量

    /** slowlog */
    slowlog_manager_t* slowlog_manager;       // 慢日志管理器

    /** metric */
    metric_t* metric;                         // 性能指标统计
    long long metric_stat_numcommands;        // 命令执行统计数量

    /* ========== 主从复制字段 ========== */
    /** slave 侧：当前节点作为 slave 时的配置与状态 */
    sds master_host;               /* master 的主机地址（NULL 表示未配置 slave 模式） */
    int master_port;               /* master 的端口号 */
    repl_state_t repl_state;       /* 当前复制状态（见 repl_state_t 枚举） */
    connection* repl_conn;         /* 与 master 建立的 TCP 连接（slave 侧） */
    sds repl_transfer_buf;         /* 接收全量数据时的缓冲区（LDB 内容） */
    long long repl_master_initial_offset; /* 全量同步时 master 的初始 offset */
    long long repl_read_offset;    /* slave 已读取并处理的 master 数据偏移量 */

    /** master 侧：当前节点作为 master 时管理 slave 列表 */
    list_t* slaves;                /* 已连接的 slave 客户端列表（CLIENT_SLAVE 标志的客户端） */
    long long repl_offset;         /* master 当前复制数据偏移量（每次写命令后递增） */

    // /** expire */
    // eb* expires;                           // 过期键处理（暂未实现）
} redis_server_t;

/**
 * 输入: 服务器实例指针
 * 输出/返回: 无返回值
 * 功能: 更新服务器缓存时间
 */
void update_cache_time(struct redis_server_t* server);

/**
 * 输入: redis_server 服务器实例指针, argc 参数个数, argv 参数数组
 * 输出/返回: 成功返回1，失败返回0
 * 功能: 启动Redis服务器，完成初始化和启动流程
 */
int start_redis_server(redis_server_t* redis_server, int argc, sds* argv);

/**
 * 输入: redis_server 服务器实例指针
 * 输出/返回: 成功返回1，失败返回0
 * 功能: 初始化Redis服务器定时任务
 */
int init_redis_server_crons(redis_server_t* redis_server);

/**
 * 输入: redis_server 服务器实例指针
 * 输出/返回: 成功返回1，失败返回0
 * 功能: 初始化Redis服务器数据库
 */
int init_redis_server_dbs(redis_server_t* redis_server);

/**
 * 输入: server 服务器实例指针
 * 输出/返回: 成功返回1，失败返回0
 * 功能: 初始化Redis模块系统
 */
int init_redis_modules(redis_server_t* server);

/* ========== 主从复制函数声明 ========== */

/**
 * 输入: server - 服务器实例
 * 输出/返回: 无
 * 功能: 初始化复制模块相关字段（在 init_redis_server 中调用）
 */
void replication_init(redis_server_t* server);

/**
 * 输入: server - 服务器实例, host - master 主机地址, port - master 端口
 * 输出/返回: 0 成功，-1 失败
 * 功能: slave 侧：开始连接到 master，触发 PING → PSYNC → 全量同步流程
 */
int replication_start_connect(redis_server_t* server, const char* host, int port);

/**
 * 输入: server - 服务器实例
 * 输出/返回: 无
 * 功能: slave 侧：断开与 master 的连接，停止复制，恢复为普通主节点
 */
void replication_stop(redis_server_t* server);

/**
 * 输入: server - 服务器实例, slave_client - 请求全量同步的 slave 客户端
 * 输出/返回: 无
 * 功能: master 侧：对请求 SYNC 的 slave 客户端执行全量数据序列化并发送
 */
void replication_full_sync_to_slave(redis_server_t* server, struct redis_client_t* slave_client);

/**
 * 输入: server - 服务器实例
 * 输出/返回: 无
 * 功能: master 侧：定时向所有已连接的 slave 推送 backlog 中的增量命令
 */
void replication_propagate_to_slaves(redis_server_t* server);

/** 模块命令处理函数 */

/**
 * 输入: server 服务器实例指针
 * 输出/返回: 无返回值
 * 功能: 注册核心模块API接口
 */
void module_register_core_api(redis_server_t* server);

/**
 * 输入: c 客户端连接指针
 * 输出/返回: 无返回值
 * 功能: 处理模块帮助命令
 */
void module_help_command(redis_client_t* c);

/**
 * 输入: c 客户端连接指针
 * 输出/返回: 无返回值
 * 功能: 处理模块列表命令，显示已加载的模块
 */
void module_list_command(redis_client_t* c);

/**
 * 输入: c 客户端连接指针
 * 输出/返回: 无返回值
 * 功能: 处理模块加载命令
 */
void module_load_command(redis_client_t* c);

/**
 * 输入: c 客户端连接指针
 * 输出/返回: 无返回值
 * 功能: 处理模块卸载命令
 */
void module_unload_command(redis_client_t* c);


#if __GNUC__ >= 4
#define redis_unreachable __builtin_unreachable      // GCC内建函数标记不可达代码
#else
#define redis_unreachable abort                      // 其他编译器使用abort终止程序
#endif

#ifdef __GNUC__
/**
 * 输入: file 文件名, line 行号, msg 格式化消息字符串, ... 可变参数
 * 输出/返回: 无返回值（程序终止）
 * 功能: Redis panic处理函数，记录错误信息并终止程序
 */
void _redis_panic(const char *file, int line, const char *msg, ...)
    __attribute__ ((format (printf, 3, 4)));
#else
void _redis_panic(const char *file, int line, const char *msg, ...);
#endif
#define redis_panic(...) _redis_panic(__FILE__,__LINE__,__VA_ARGS__),redis_unreachable()  // Redis panic宏，记录文件行号并终止

#endif