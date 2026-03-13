#include "shared.h"

struct shared_objects_t shared;

/**
 * 初始化常用 RESP 回复对象，提高响应性能
 * 预创建常用的协议字符串，避免运行时重复分配内存
 * 输入: 无
 * 返回: 无
 */
void init_shared_objects() {
    int j;
    /* 初始化基本协议字符串 */
    shared.crlf = latte_object_string_new(sds_new("\r\n"));
    shared.ok = latte_object_string_new(sds_new("+OK\r\n"));
    shared.pong = latte_object_string_new(sds_new("+PONG\r\n"));
    shared.wrongtypeerr = latte_object_string_new(sds_new("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"));

    /* 预生成 0..31 的 RESP 头部，提高小数值响应的性能 */
    for (j = 0; j < OBJ_SHARED_BULKHDR_LEN; j++) {
        shared.mbulkhdr[j] = latte_object_string_new(
            sds_cat_printf(sds_empty(),"*%d\r\n",j));  /* 多批量回复头部 */
        shared.bulkhdr[j] = latte_object_string_new(
            sds_cat_printf(sds_empty(),"$%d\r\n",j));  /* 批量回复头部 */
    }
}