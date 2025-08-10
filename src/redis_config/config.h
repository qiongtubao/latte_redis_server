
#ifndef __REDIS_CONFIG_H
#define __REDIS_CONFIG_H
#include "config/config.h"


/*
    目前只有读取配置文件和命令行解析
    

    TODO：
    1. 对象映射
    2. 命令行修改配置
    3. 不支持bool类型和enum类型
    4. 保存配置到文件
    
*/
/* Global vars */
config_manager_t* create_server_config();

#endif