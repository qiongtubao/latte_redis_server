#ifndef __REDIS_SHARED_H
#define __REDIS_SHARED_H
#include "sds/sds.h"
#include "../object/string.h"
#define OBJ_SHARED_BULKHDR_LEN 32    /* 共享批量头数量，预生成 0..31 的 RESP 头部 */
typedef struct shared_objects_t {
   latte_object_t* crlf,              /* "\r\n" 回车换行符 */
                 * ok,                /* "+OK\r\n" 成功响应 */
                 * pong,              /* "+PONG\r\n" PING 命令响应 */
                 * wrongtypeerr,      /* 错误类型操作的错误消息 */
   *mbulkhdr[OBJ_SHARED_BULKHDR_LEN], /* "*<value>\r\n" 多批量回复头部数组 */
   *bulkhdr[OBJ_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" 批量回复头部数组 */
} shared_objects_t;
extern struct shared_objects_t shared;

/**
 * 初始化常用 RESP 回复对象，减少运行时字符串创建开销
 * 输入: 无
 * 返回: 无
 */
void init_shared_objects();

#endif