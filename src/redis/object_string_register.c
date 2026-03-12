/**
 * object_string 类型注册到 object_manager
 * 提供 create、release、save、load、calc 函数
 */

#include "object/object.h"
#include "object/object_manager.h"
#include "../object/string.h"
#include "odb/odb.h"
#include "error/error.h"
#include "sds/sds.h"
#include "zmalloc/zmalloc.h"

/**
 * 创建字符串对象（工厂函数，type 会被 object_manager 覆盖）
 * 输入: 无
 * 返回: 创建的字符串对象，失败返回NULL
 */
static latte_object_t* object_string_create(void) {
    sds s = sds_empty();  // 创建空的sds字符串
    if (!s) return NULL;
    latte_object_t* o = (latte_object_t*)zmalloc(sizeof(latte_object_t));
    if (!o) {
        sds_delete(s);
        return NULL;
    }
    o->type = 0; /* 会被 object_manager 覆盖为 type_id */
    o->lru = 0;
    o->refcount = 1;
    o->ptr = s;  // 设置sds指针
    return o;
}

/**
 * 释放字符串对象的 sds
 * 输入: obj - 要释放的字符串对象
 * 返回: 无
 */
static void object_string_release(latte_object_t* obj) {
    if (!obj || !obj->ptr) return;
    sds s = (sds)obj->ptr;  // 获取sds指针
    sds_delete(s);  // 释放sds内存
    obj->ptr = NULL;
}

/**
 * 将字符串序列化到 oio（用于 SAVE 命令）
 * 输入: o - 输出流, obj - 字符串对象
 * 返回: latte_error_t 错误码
 */
static latte_error_t object_string_save(oio *o, latte_object_t *obj) {
    if (!obj || !obj->ptr) {
        latte_error_t err = {CInvalidArgument, NULL};
        return err;
    }
    sds s = (sds)obj->ptr;  // 获取sds字符串
    if (odb_write_string(o, s, sds_len(s)) == 0) {  // 将字符串写入输出流
        latte_error_t err = {CIOError, NULL};
        return err;
    }
    latte_error_t ok = {COk, NULL};
    return ok; /* success */
}

/**
 * 从 oio 反序列化字符串（用于 LOAD 命令）
 * 输入: o - 输入流, out_obj - 输出对象指针
 * 返回: NULL-成功, 非NULL-错误信息
 */
static latte_error_t* object_string_load(oio *o, latte_object_t **out_obj) {
    sds s = odb_read_string(o);  // 从输入流读取字符串
    if (!s) {
        return error_new(CIOError, "object_string_load", "failed to read string");
    }
    latte_object_t* obj = (latte_object_t*)zmalloc(sizeof(latte_object_t));
    if (!obj) {
        sds_delete(s);
        return error_new(CCorruption, "object_string_load", "failed to create object");
    }
    obj->type = 0; /* 会被 object_manager 覆盖为 type_id */
    obj->lru = 0;
    obj->refcount = 1;
    obj->ptr = s;  // 设置sds指针
    *out_obj = obj;
    return NULL; /* success */
}

/**
 * 计算字符串对象的 hash（DJB2 算法）
 * 输入: obj - 字符串对象
 * 返回: hash值
 */
static uint64_t object_string_calc(latte_object_t *obj) {
    if (!obj || !obj->ptr) return 0;
    sds s = (sds)obj->ptr;
    /* 使用 DJB2 hash 算法 */
    uint64_t hash = 5381;
    size_t len = sds_len(s);
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)s[i];  // hash * 33 + c
    }
    return hash;
}

/**
 * 注册 string 类型到 object_manager
 * 输入: 无
 * 返回: 0-成功, -1-失败
 */
int register_object_string_type(void) {
    return object_manager_register(
        "string",
        object_string_create,
        object_string_release,
        object_string_save,
        object_string_load,
        object_string_calc
    );
}
