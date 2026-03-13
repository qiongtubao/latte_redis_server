/**
 * @file version.c
 * @brief 版本信息显示模块
 *
 * 负责输出Latte服务器的版本信息和系统架构信息
 */

#include "version.h"
#include <stdio.h>

/**
 * @brief 打印版本信息
 *
 * 输出Latte服务器的版本号和系统架构位数信息
 *
 * @return int 成功返回1
 */
int version() {
    //sha=%s:%d
    //redisGitSHA1(),
    // atoi(redisGitDirty()) > 0,

    // malloc=%s
    //ZMALLOC_LIB,

    // build=%llx
    //(unsigned long long) redisBuildId()

    // 打印版本号和系统架构信息
    printf(
        "Latte server v=%s "
        "bits=%d \n",
        LATTE_VERSION,
        sizeof(long) == 4 ? 32 : 64  // 根据long类型大小判断系统是32位还是64位
    );
    return 1;
}