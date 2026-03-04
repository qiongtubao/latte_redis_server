/**
 * object_string 类型注册到 object_manager
 * 提供 create、release、save、load 函数
 */

#include "object/object.h"
#include "object/object_manager.h"
#include "../object/string.h"
#include "odb/odb.h"
#include "error/error.h"
#include "sds/sds.h"
#include "zmalloc/zmalloc.h"

/* object_string 的创建函数 */
/* 注意：object_manager 要求返回的对象的首字节会被覆盖为 type_id */
static latte_object_t* object_string_create(void) {
    sds s = sds_empty();
    if (!s) return NULL;
    latte_object_t* o = (latte_object_t*)zmalloc(sizeof(latte_object_t));
    if (!o) {
        sds_delete(s);
        return NULL;
    }
    o->type = 0; /* 会被 object_manager 覆盖为 type_id */
    o->lru = 0;
    o->refcount = 1;
    o->ptr = s;
    return o;
}

/* object_string 的释放函数 */
static void object_string_release(latte_object_t* obj) {
    if (!obj || !obj->ptr) return;
    sds s = (sds)obj->ptr;
    sds_delete(s);
    obj->ptr = NULL;
}

/* object_string 的保存函数 */
static latte_error_t object_string_save(oio *o, latte_object_t *obj) {
    if (!obj || !obj->ptr) {
        latte_error_t err = {CInvalidArgument, NULL};
        return err;
    }
    sds s = (sds)obj->ptr;
    if (odb_write_string(o, s, sds_len(s)) == 0) {
        latte_error_t err = {CIOError, NULL};
        return err;
    }
    latte_error_t ok = {COk, NULL};
    return ok; /* success */
}

/* object_string 的加载函数 */
/* 注意：返回的对象的首字节会被 object_manager 覆盖为 type_id */
static latte_error_t* object_string_load(oio *o, latte_object_t **out_obj) {
    sds s = odb_read_string(o);
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
    obj->ptr = s;
    *out_obj = obj;
    return NULL; /* success */
}

/* object_string 的计算函数（用于 hash 等） */
static uint64_t object_string_calc(latte_object_t *obj) {
    if (!obj || !obj->ptr) return 0;
    sds s = (sds)obj->ptr;
    /* 简单的 hash 计算 */
    uint64_t hash = 5381;
    size_t len = sds_len(s);
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)s[i];
    }
    return hash;
}

/* 注册 object_string 类型到 object_manager */
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
