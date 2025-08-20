
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
    int64_t port;
    vector_t* bind;
    int64_t tcp_backlog;
    sds logfile;
    log_level_enum log_level;
    int64_t max_clients;
    bool use_async_io;
    int64_t event_loop_size;
    int64_t hz;
    int64_t db_num;
    vector_t* load_modules;
} server_config_t;

/* Global vars */
server_config_t* server_config_new(config_manager_t* manager);
void server_config_delete(server_config_t* config);

#endif