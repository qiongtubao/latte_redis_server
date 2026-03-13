
#ifndef __REDIS_CONFIG_H
#define __REDIS_CONFIG_H
#include "config/config.h"
#include "log/log.h"

/*
    目前只有读取配置文件和命令行解析
    

    TODO：
    1. 对象映射
    2. 命令行修改配置
    3. 不支持bool类型和enum类型
    4. 保存配置到文件
    
*/


typedef struct server_config_t {
    long long port;                        /* 服务器监听端口号 */
    vector_t* bind;                        /* 绑定的网络接口地址列表 */
    long long tcp_backlog;                 /* TCP 连接队列最大长度 */
    sds logfile;                          /* 日志文件路径 */
    sds ldb_file;                         /* dump.ldb 文件路径 (SAVE/LOAD 默认文件) */
    log_level_enum log_level;             /* 日志级别 (trace/debug/info/warn/error/fatal) */
    long long max_clients;                /* 最大客户端连接数 */
    bool use_async_io;                    /* 是否使用异步 I/O */
    long long event_loop_size;            /* 事件循环处理的最大事件数 */
    long long hz;                         /* 服务器主循环频率 (每秒执行次数) */
    long long db_num;                     /* 数据库数量 */
    vector_t* load_modules;               /* 需要加载的模块列表 */
    long long slowlog_log_slower_than;    /* 慢查询日志阈值 (微秒) */
    long long slowlog_max_len;            /* 慢查询日志最大条数 */
} server_config_t;

/* Global vars */
/**
 * 创建服务器配置结构体并注册所有配置项规则
 * 输入: config_manager - 配置管理器实例
 * 返回: 初始化完成的服务器配置结构体指针
 */
server_config_t* server_config_new(config_manager_t* manager);

/**
 * 释放服务器配置结构体
 * 输入: config - 待释放的配置结构体指针
 * 返回: 无
 */
void server_config_delete(server_config_t* config);

#endif