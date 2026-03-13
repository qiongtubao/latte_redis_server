#ifndef __REDIS_OBJECT_STRING_H__
#define __REDIS_OBJECT_STRING_H__

#include "object/object.h"
#include "sds/sds.h"
#include "object/object_manager.h"
#include <string.h>

/* 对象类型常量：在新的 object_manager 系统中，type 是动态分配的 type_id
 * 为了兼容性，我们使用一个函数来获取 "string" 类型的 type_id
 * 注意：这个值在 register_object_string_type 之后才有效
 */
/* 临时定义：假设 "string" 是第一个注册的类型，type_id 为 0 */
#define OBJ_STRING 0

/**
 * 检查对象是否为 sds 编码的字符串
 * 输入: obj - latte对象指针
 * 返回: 1-是sds编码字符串, 0-不是
 */
int sds_encoded_object(latte_object_t* obj);

/**
 * 用 object_manager 创建字符串对象
 * 输入: s - sds字符串
 * 返回: 创建的latte对象指针，失败返回NULL
 */
latte_object_t* latte_object_string_new(sds s);

/**
 * 获取字符串对象长度（通过类型名检查）
 * 输入: obj - latte对象指针
 * 返回: 字符串长度，失败返回0
 */
static inline size_t string_object_len(latte_object_t *obj) {
    if (!obj || !obj->ptr) return 0;
    /* 在新的 object_manager 系统中，type 是 type_id，需要通过类型名检查 */
    const char* type_name = object_manager_get_type_name((uint8_t)obj->type);
    if (type_name && strcmp(type_name, "string") == 0) {
        return sds_len((sds)obj->ptr);
    }
    return 0;
}

/**
 * 从对象获取 sds 指针（类型安全）
 * 输入: obj - latte对象指针, out - 输出sds指针的地址
 * 返回: 0-成功, -1-失败
 */
static inline int get_sds_from_object(latte_object_t *obj, sds *out) {
    if (!obj || !out) return -1;
    /* 在新的 object_manager 系统中，type 是 type_id，需要通过类型名检查 */
    const char* type_name = object_manager_get_type_name((uint8_t)obj->type);
    if (type_name && strcmp(type_name, "string") == 0 && obj->ptr) {
        *out = (sds)obj->ptr;
        return 0;
    }
    return -1;
}

#endif /* __REDIS_OBJECT_STRING_H__ */
