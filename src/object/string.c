/**
 * latte_object_string_new 实现：使用 object_manager 创建字符串对象
 */

#include "string.h"
#include "../../deps/latte_c/src/object/object_manager.h"
#include "sds/sds.h"
#include "zmalloc/zmalloc.h"

/* 使用 object_manager 创建字符串对象 */
latte_object_t* latte_object_string_new(sds s) {
    
    if (!s) {
        return NULL;
    }
    
    /* 使用 object_manager 创建包装的对象 */
    latte_object_t* obj = object_manager_create_wrapped("string");
    if (!obj) {
        LATTE_LIB_LOG(LOG_ERROR, "latte_object_string_new: object_manager_create_wrapped failed");
        return NULL;
    }
    
    /* 设置 ptr 为传入的 sds */
    /* 注意：object_manager_create_wrapped 已经通过 create_fn 创建了一个空的 sds，
     * 我们需要释放它并设置为我们传入的 sds */
    if (obj->ptr) {
        sds_delete((sds)obj->ptr);
    }
    obj->ptr = s;
    
    return obj;
}

int sds_encoded_object(latte_object_t* obj) {
    if (!obj || obj->ptr == NULL) return 0;
    
    const char* type_name = object_manager_get_type_name((uint8_t)obj->type);
    if (type_name && strcmp(type_name, "string") == 0) {
        return 1;
    }
    return 0;
}