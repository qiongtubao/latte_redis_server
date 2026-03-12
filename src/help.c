/**
 * @file help.c
 * @brief 帮助信息显示模块
 *
 * 负责输出Latte服务器的使用说明和命令行参数帮助信息
 */

#include "help.h"
#include <stdio.h>

/**
 * @brief 显示程序使用帮助信息
 *
 * 输出详细的命令行用法说明，包括各种启动选项和示例
 *
 * @return int 成功返回1
 */
int help() {
    // 输出基本用法说明
    fprintf(stderr,"Usage: ./latte-server [/path/to/latte.conf] [options] [-]\n");
    fprintf(stderr,"       ./latte-server - (read config from stdin)\n");
    fprintf(stderr,"       ./latte-server -v or --version\n");
    fprintf(stderr,"       ./latte-server -h or --help\n");
    fprintf(stderr,"       ./latte-server --test-memory <megabytes>\n");
    fprintf(stderr,"       ./latte-server --check-system\n");
    fprintf(stderr,"\n");

    // 输出使用示例
    fprintf(stderr,"Examples:\n");
    fprintf(stderr,"       ./latte-server (run the server with default conf)\n");
    fprintf(stderr,"       echo 'maxmemory 128mb' | ./latte-server -\n");
    fprintf(stderr,"       ./latte-server /etc/latte/6379.conf\n");
    fprintf(stderr,"       ./latte-server --port 7777\n");
    fprintf(stderr,"       ./latte-server --port 7777 --replicaof 127.0.0.1 8888\n");
    fprintf(stderr,"       ./latte-server /etc/mylatte.conf --loglevel verbose -\n");
    fprintf(stderr,"       ./latte-server /etc/mylatte.conf --loglevel verbose\n\n");

    // 输出Sentinel模式说明
    fprintf(stderr,"Sentinel mode:\n");
    fprintf(stderr,"       ./latte-server /etc/sentinel.conf --sentinel\n");
    return 1;
}