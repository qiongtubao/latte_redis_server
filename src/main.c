/**
 * @file main.c
 * @brief Latte Redis服务器主程序入口文件
 *
 * 负责解析命令行参数，判断运行模式，并分发到对应的处理函数
 */

#include "main.h"

#include "version.h"
#include "help.h"
#include "env.h"
#include "sds/sds.h"
#include <string.h>
#include "server/server.h"

// 全局Redis服务器实例
struct redis_server_t redis_server;

/**
 * @brief 解析命令行参数并转换为sds字符串数组
 *
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @param len 输出参数，返回解析后的参数个数
 * @return sds* 返回解析后的sds字符串数组，需要调用者释放内存
 */
sds* parseArgv(int argc, char** argv, int* len) {
    // 分配内存存储sds字符串数组，多分配一个位置用于NULL结尾
    char** result = zmalloc(sizeof(sds)*(argc + 1));
    //argv[0] is config path
    // result[0] = getAbsolutePath(argv[0]);
    result[argc] = NULL; // 数组末尾设置为NULL

    // 将每个命令行参数转换为sds字符串
    for(int j = 0; j < argc; j++) {
        result[j] = sds_new_len(argv[j], strlen(argv[j]));
    }
    *len = argc; // 返回参数个数
    return result;
}

/**
 * @brief 程序运行模式枚举
 *
 * 定义了程序可能的运行模式，用于根据命令行参数判断执行哪种操作
 */
typedef enum {
    HELP = 0,        // 显示帮助信息模式
    VERSION,         // 显示版本信息模式
    TEST_MEMORY,     // 内存测试模式
    CHECK_SYSTEM,    // 系统检查模式
    ENV,             // 环境信息显示模式
    SERVER,          // 服务器运行模式（默认模式）
} Mode;

/**
 * @brief 根据命令行参数判断程序运行模式
 *
 * @param argc 命令行参数个数
 * @param argv 解析后的sds字符串数组
 * @return Mode 返回对应的运行模式枚举值
 */
Mode getMode(int argc, sds* argv) {
    // 如果只有程序名一个参数，默认启动服务器模式
    if (argc == 1) return SERVER;

    // 检查帮助参数
    if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) {
        return HELP;
    }

    // 检查版本参数
    if (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0) {
        return VERSION;
    }

    // 检查环境信息参数
    if (strcmp(argv[1], "--env") == 0) {
        return ENV;
    }

    // 检查内存测试参数
    if (strcmp(argv[1], "--test-memory") == 0) {
        return TEST_MEMORY;
    }

    // 检查系统检查参数
    if (strcmp(argv[1], "--check-system") == 0) {
        return CHECK_SYSTEM;
    }

    // 默认返回服务器模式
    return SERVER;
}

/**
 * @brief 程序主入口函数
 *
 * 解析命令行参数，判断运行模式，并分发到相应的处理函数
 *
 * @param argc 命令行参数个数
 * @param argv 命令行参数字符串数组
 * @return int 程序退出状态码，成功返回1
 */
int main(int argc, char **argv) {
    int exec_argc;
    // 解析命令行参数为sds字符串数组
    sds* exec_argv = parseArgv(argc, argv, &exec_argc);

    // 根据参数判断运行模式
    Mode mode = getMode(exec_argc, exec_argv);

    // 根据模式分发到对应的处理函数
    switch (mode) {
        case HELP:
            help();           // 显示帮助信息
            goto end;
        case ENV:
            env();            // 显示环境信息
            goto end;
        case VERSION:
            version();        // 显示版本信息
            goto end;
        case SERVER:
            start_redis_server(&redis_server, exec_argc, exec_argv);  // 启动Redis服务器
            goto end;
        default:
            break;
    }

end:
    // 释放解析的命令行参数内存
    for(int i = 0; i < exec_argc; i++) {
        sds_delete(exec_argv[i]);  // 释放每个sds字符串
    }
    zfree(exec_argv);  // 释放sds数组内存
    return 1;
}