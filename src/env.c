/**
 * @file env.c
 * @brief 环境信息显示模块
 *
 * 用于输出当前系统的环境信息，包括事件循环API等
 */

#include "env.h"
#include <stdio.h>
#include "ae/ae.h"

/**
 * @brief 显示当前环境信息
 *
 * 输出latte服务器运行环境的相关信息，包括事件循环API名称
 *
 * @return int 成功返回1
 */
int env() {
    fprintf(stderr,"latte_env: \n");
    // 输出当前使用的事件循环API名称（如epoll、kqueue等）
    fprintf(stderr,"       %s\n", ae_get_api_name());
    return 1;
}