#include "db.h"
#include "../object/string.h"
#include "debug/latte_debug.h"
#include "utils/utils.h"
#include "dict/dict_plugins.h"
#include "time/localtime.h"
#include <limits.h>

/* 常量定义 */
#define LRU_CLOCK_MAX ((1<<LRU_BITS)-1)
#define OBJ_SHARED_REFCOUNT INT_MAX
#define OBJ_STATIC_REFCOUNT (INT_MAX-1)

static void cumulative_key_count_add(kv_store_t* kvs, int didx, long delta);

int get_kv_store_index_for_key(sds key) {
    return 0;
}


int db_add_key_value(redis_server_t* server,redis_db_t* db, latte_object_t* key, latte_object_t* value) {
    return db_add_key_value_internal(server, db, key, value, 0);
}



int db_set_value(redis_server_t* server,redis_db_t* db, latte_object_t* key, latte_object_t* val, int overwrite, dict_entry_t* de) {
    sds key_ptr;
    latte_assert_with_info(get_sds_from_object(key, &key_ptr) == 0, "key is not a string");
    int dict_index = get_kv_store_index_for_key(key_ptr);
    if (!de) de = kv_store_dict_find(db->keys, dict_index, key_ptr);
    latte_assert_with_info(de != NULL, "[db_set_value] kv_store dict unfind key %s", key_ptr);
    latte_object_t* old = dict_get_entry_val(de);
    val->lru = old->lru;

    if (overwrite) {
        /* 原地覆盖：先减旧值引用，再设置新值（LOAD 等场景下允许同 key 覆盖） */
    }
    kv_store_dict_set_val(db->keys, dict_index, de, val);
    // if (try_offload_free_obj_to_io_threads(old) == C_OK) {

    // }
    // else if (server.lazyfree_server_del) {
        // free_obj_async(server, key, old, db->id);
    // }
    // else {
        latte_object_decr_ref_count(old);
    // }
    return 0;
}


unsigned long lfu_get_time_in_minutes(redis_server_t* server) {
    return (server->unixtime/60) & 65535;
}
#define LRU_CLOCK_RESOLUTION 1000

unsigned int get_lru_clock(redis_server_t* server) {
    return (mstime() / LRU_CLOCK_RESOLUTION) & LRU_CLOCK_MAX;
}

unsigned int lru_clock(redis_server_t* server) {
    unsigned int lruclock;
    if (1000/server->hz <= LRU_CLOCK_RESOLUTION) {
        lruclock = server->lruclock;
    } else {
        lruclock = get_lru_clock(server);
    }
    return lruclock;
}

#define LFU_INIT_VAL 5
void init_object_lru_or_lfu(redis_server_t* server, latte_object_t* o) {
    if (o->refcount == OBJ_SHARED_REFCOUNT) return;

    if (server->maxmemory_policy & MAXMEMORY_FLAG_LFU) {
        o->lru = (lfu_get_time_in_minutes(server) << 8) | LFU_INIT_VAL;
    } else {
        o->lru = lru_clock(server);
    }
    return;
} 

int db_add_key_value_internal(redis_server_t* server,redis_db_t* db, latte_object_t* key, latte_object_t* val, int update_if_existing) {
    dict_entry_t *existing;
    sds key_ptr;
    latte_assert_with_info(get_sds_from_object(key, &key_ptr) == 0, "key is not a string");// "key is not a string"
    int dict_index = get_kv_store_index_for_key(key_ptr);
    sds new_key = sds_new(key->ptr);
    dict_entry_t* de = kv_store_dict_add_raw(db->keys, dict_index, new_key, &existing);
    if (existing) {
        sds_delete(new_key);
        /* Key already exists: update in place (e.g. SET overwrite) to avoid assert crash */
        db_set_value(server, db, key, val, 1, existing);
        return 0;
    }
    if (de == NULL) {
        sds_delete(new_key);
        return -1;
    }
    init_object_lru_or_lfu(server, val);
    kv_store_dict_set_val(db->keys, dict_index, de, val);
    // signal_key_as_ready(db, key, val->type);
    // notify_keyspace_event(NOTIFY_NEW, "new", key, db->id);
    return 0;
}


/* kv_store_t function */

dict_t* kv_store_get_dict(kv_store_t* kvs, int didx) {
    return kvs->dicts[didx];
}

#define dict_meta_data(d) (&(d)->metadata)
static dict_t* kv_store_create_dict_if_needed(kv_store_t* kvs, int didx) {
    dict_t *d = kv_store_get_dict(kvs, didx);
    if (d) return d;
    kvs->dicts[didx] = dict_new(kvs->dtype); //延迟创建
    kv_store_dict_meta_data_t* meta_data = (kv_store_dict_meta_data_t*)dict_meta_data(kvs->dicts[didx]);
    // meta_data->kvs = kvs;
    kvs->allocated_dicts++;  //可用字典个数   无法用vector原因时vector是顺序的
    return kvs->dicts[didx];
}

dict_entry_t* kv_store_dict_find(kv_store_t* kvs, int didx, void* key) {
    dict_t* d = kv_store_get_dict(kvs, didx);
    if (d == NULL) return NULL;
    return dict_find(d, key);
}

int kv_store_dict_delete_key(kv_store_t* kvs, int didx, const void* key) {
    dict_t* d = kv_store_get_dict(kvs, didx);
    if (d == NULL) return DICT_ERR;
    int ret = dict_delete_key(d, key);
    if (ret == DICT_OK)
        cumulative_key_count_add(kvs, didx, -1);
    return ret;
}



static void cumulative_key_count_add(kv_store_t* kvs, int didx, long delta) {
    kvs->key_count += delta; //总个数统计

    dict_t* d = kv_store_get_dict(kvs, didx);
    size_t dsize = dict_size(d);
    int non_empty_dicts_delta = dsize == 1 ? 1 : dsize == 0? -1:0; //从0到1 则+1，如果是0的话-1
    kvs->non_empty_dicts += non_empty_dicts_delta;

    if (kvs->num_dicts == 1) return;

    int idx = didx + 1;
    // 当当前索引 idx 还没有超过最大字典数量时，继续循环
    while (idx <= kvs->num_dicts) {
        if (delta < 0) {
            latte_assert_with_info(kvs->dict_size_index[idx] >= (unsigned long long)labs(delta), "kvs->dict_size_index[idx] is less than labs(delta)");
        }
        // 更新当前位置的值：加上 delta（可能是正也可能是负）
        kvs->dict_size_index[idx] += delta;
        // 根据二进制位技巧，找到下一个需要更新的父节点位置
        // (idx & -idx) 得到的是最低位的1所代表的数值
        // 比如 idx = 6 (0110)，则 idx & -idx = 2 (0010)
        idx += (idx & -idx);
    }

}

dict_entry_t* kv_store_dict_add_raw(kv_store_t* kvs, int didx, void *key, dict_entry_t** existing) {
    dict_t* d = kv_store_create_dict_if_needed(kvs, didx);
    dict_entry_t* ret = dict_add_raw(d, key, existing);
    if (ret) cumulative_key_count_add(kvs, didx, 1);
    return ret;
}

int kv_store_dict_set_val(kv_store_t* kvs, int didx, dict_entry_t* de, void* val) {
    dict_t * d = kv_store_get_dict(kvs, didx);
    dict_set_val(d, de, val);
    return 0;
}

/* ---------- Expire helpers ---------- */
long long db_get_expire(redis_db_t* db, sds key) {
    int didx = get_kv_store_index_for_key(key);
    dict_entry_t* de = kv_store_dict_find(db->expires, didx, key);
    if (!de) return 0;
    return (long long)(uintptr_t)dict_get_entry_val(de);
}

int db_set_expire(redis_server_t* server, redis_db_t* db, sds key, long long when_ms) {
    UNUSED(server);
    int didx = get_kv_store_index_for_key(key);
    dict_entry_t* existing = NULL;
    dict_entry_t* de = kv_store_dict_add_raw(db->expires, didx, key, &existing);
    if (!de) return -1;
    kv_store_dict_set_val(db->expires, didx, de, (void*)(uintptr_t)when_ms);
    return 0;
}

void db_remove_expire(redis_db_t* db, sds key) {
    int didx = get_kv_store_index_for_key(key);
    kv_store_dict_delete_key(db->expires, didx, key);
}

void db_delete_key(redis_server_t* server, redis_db_t* db, sds key) {
    UNUSED(server);
    int didx = get_kv_store_index_for_key(key);
    kv_store_dict_delete_key(db->expires, didx, key);
    kv_store_dict_delete_key(db->keys, didx, key);
}

int expire_if_needed(redis_server_t* server, redis_db_t* db, sds key) {
    dict_entry_t* de = kv_store_dict_find(db->expires, get_kv_store_index_for_key(key), key);
    if (!de) return 0;
    long long when_ms = (long long)(uintptr_t)dict_get_entry_val(de);
    if (mstime() < when_ms) return 0;
    db_delete_key(server, db, key);
    return 1;
}

int kv_store_dict_set_key(kv_store_t* kvs, int didx, dict_entry_t* de, void* key) {
    dict_t * d = kv_store_get_dict(kvs, didx);
    dict_set_key(d, de, key);
    return 0;
}

void dict_object_destructor(dict_t* d, void* val) {
    UNUSED(d);
    if (val == NULL) return;
    latte_object_decr_ref_count(val);
}


int over_maxmemory_after_alloc(size_t moremem) {
    // if (!server.maxmemory)
    return 0;
    // size_t mem_used = zmalloc_used_memory();
    // if (mem_used + moremem <= server.maxmemory) return 0;
    // size_t overhead = free_memory_get_not_counted_memory();
    // mem_used = (mem_used > overhead) ? mem_used - overhead: 0;
    // return mem_used + moremem > server.maxmemory;
}

#define HASHTABLE_MAX_LOAD_FACTOR 1.618
/* Return 1 if currently we allow dict to expand. Dict may allocate huge
 * memory to contain hash buckets when dict expands, that may lead the server to
 * reject user's requests or evict some keys, we can stop dict to expand
 * provisionally if used memory will be over maxmemory after dict expands,
 * but to guarantee the performance of the server, we still allow dict to expand
 * if dict load factor exceeds HASHTABLE_MAX_LOAD_FACTOR. */
int dict_resize_allowed(size_t moreMem, double usedRatio) {
    /* for debug purposes: dict is not allowed to be resized. */
    // if (!server.dict_resizing) return 0;

    if (usedRatio <= HASHTABLE_MAX_LOAD_FACTOR) {
        return !over_maxmemory_after_alloc(moreMem);
    } else {
        return 1;
    }
}

/* Returns the size of the DB dict metadata in bytes. */
size_t kv_store_dict_meta_data_size(dict_t *d) {
    UNUSED(d);
    return sizeof(kv_store_dict_meta_data_t);
}


dict_func_t kv_store_keys_dict_type = {
    dict_sds_hash,
    NULL,
    NULL,
    dict_sds_key_compare,
    dict_sds_destructor,
    dict_object_destructor,
    dict_resize_allowed,
    // kv_store_dict_rehashing_started,
    // kv_store_dict_rehashing_completed,
    kv_store_dict_meta_data_size,
    // .embed_key = dict_sds_embed_key,
    // .embedded_entry = 1,
};

/* Expires dict: key = sds (dup on add), val = (void*)(uintptr_t) mstime */
dict_func_t kv_store_expires_dict_type = {
    dict_sds_hash,
    dict_sds_dup,
    NULL,
    dict_sds_key_compare,
    dict_sds_destructor,
    NULL,
    dict_resize_allowed,
    kv_store_dict_meta_data_size,
};




#define KVSTORE_ALLOCATE_DICTS_ON_DEMAND (1 << 0)
kv_store_t *kv_store_create(dict_func_t *type, int num_dicts_bits, int flags) {
    /* We can't support more than 2^16 dicts because we want to save 48 bits
     * for the dict cursor, see kvstoreScan */
    latte_assert_with_info(num_dicts_bits <= 16, "num_dicts_bits is too large");

    /* The dictType of kvstore needs to use the specific callbacks.
     * If there are any changes in the future, it will need to be modified. */
    // assert(type->rehashingStarted == kvstoreDictRehashingStarted);
    // assert(type->rehashingCompleted == kvstoreDictRehashingCompleted);
    latte_assert_with_info(type->dictEntryMetadataBytes == kv_store_dict_meta_data_size, "type->dictEntryMetadataBytes is not equal to kv_store_dict_meta_data_size");

    kv_store_t *kvs = zcalloc(sizeof(*kvs));
    kvs->dtype = type;
    kvs->flags = flags;

    kvs->num_dicts_bits = num_dicts_bits;
    kvs->num_dicts = 1 << kvs->num_dicts_bits;
    kvs->dicts = zcalloc(sizeof(dict_t *) * kvs->num_dicts);

    if (!(kvs->flags & KVSTORE_ALLOCATE_DICTS_ON_DEMAND)) {
        for (int i = 0; i < kvs->num_dicts; i++) {
            LATTE_LIB_LOG(LOG_INFO, "DB CREATE");
            kv_store_create_dict_if_needed(kvs, i);
        }
    } else {
        for (int i = 0; i < kvs->num_dicts; i++) {
            kvs->dicts[i] = NULL;
        }
    }

    kvs->rehashing = list_new();
    kvs->key_count = 0;
    kvs->non_empty_dicts = 0;
    kvs->resize_cursor = 0;
    kvs->dict_size_index = kvs->num_dicts > 1 ? zcalloc(sizeof(unsigned long long) * (kvs->num_dicts + 1)) : NULL;
    kvs->bucket_count = 0;
    kvs->overhead_hashtable_lut = 0;
    kvs->overhead_hashtable_rehashing = 0;

    return kvs;
}

uint64_t dict_object_hash(const void *key) {
    const latte_object_t *o = key;
    return dict_gen_hash_function(o->ptr, sds_len((sds)o->ptr));
}

int dict_object_key_compare(dict_t *d, const void *key1, const void *key2) {
    const latte_object_t *o1 = key1, *o2 = key2;
    return dict_sds_key_compare(d, o1->ptr, o2->ptr);
}

void dict_list_destructor(dict_t *d, void *val) {
    UNUSED(d);
    list_delete((list_t *)val);
}

dict_func_t key_list_dict_type = {
    dict_object_hash,
    NULL,
    NULL,
    dict_object_key_compare,
    dict_object_destructor,
    dict_list_destructor,
    NULL
};


uint64_t dict_encode_object_hash(const void *key) {
    latte_object_t *o = (latte_object_t *)key;

    /* 在新的 object_manager 系统中，所有字符串对象都是 sds 编码 */
    if (sds_encoded_object(o)) {
        return dict_gen_hash_function(o->ptr, sds_len((sds)o->ptr));
    } else {
        latte_panic("Unknown string encoding");
    }
}



int dict_encode_object_key_compare(dict_t *d, const void *key1, const void *key2) {
    latte_object_t *o1 = (latte_object_t *)key1, *o2 = (latte_object_t *)key2;
    int cmp;

    /* 在新的 object_manager 系统中，所有字符串对象都是 sds 编码 */
    /* Due to OBJ_STATIC_REFCOUNT, we avoid calling getDecodedObject() without
     * good reasons, because it would incrRefCount() the object, which
     * is invalid. So we check to make sure dictFind() works with static
     * objects as well. */
    /* 注意：get_decode_object 可能不存在，直接使用 ptr */
    if (o1->refcount != OBJ_STATIC_REFCOUNT && o1->ptr) {
        /* 对于非静态对象，直接使用 ptr */
    }
    if (o2->refcount != OBJ_STATIC_REFCOUNT && o2->ptr) {
        /* 对于非静态对象，直接使用 ptr */
    }
    cmp = dict_sds_key_compare(d, o1->ptr, o2->ptr);
    return cmp;
}


dict_func_t object_key_pointer_value_dict_type = {
    dict_encode_object_hash,
    NULL,
    NULL,
    dict_encode_object_key_compare,
    dict_object_destructor,
    NULL,
    NULL
};

int init_redis_server_dbs(redis_server_t* redis_server) {
    int slot_count_bits = 0;
    int flags = KVSTORE_ALLOCATE_DICTS_ON_DEMAND;
    redis_server->dbs = zmalloc(sizeof(redis_db_t) * redis_server->db_num);
    for (int i = 0; i < redis_server->db_num; i++) {
        redis_server->dbs[i].id = i;
        redis_server->dbs[i].keys = kv_store_create(&kv_store_keys_dict_type, slot_count_bits, flags);
        redis_server->dbs[i].expires = kv_store_create(&kv_store_expires_dict_type, slot_count_bits, flags);
        redis_server->dbs[i].expires_cursor = 0;
        redis_server->dbs[i].blocking_keys = dict_new(&key_list_dict_type);
        redis_server->dbs[i].blocking_keys_unblock_on_nokey = dict_new(&object_key_pointer_value_dict_type);
        redis_server->dbs[i].ready_keys = dict_new(&object_key_pointer_value_dict_type);
        redis_server->dbs[i].watched_keys = dict_new(&key_list_dict_type);
        redis_server->dbs[i].avg_ttl = 0;
        redis_server->dbs[i].defrag_later = list_new();
        list_set_free_method(redis_server->dbs[i].defrag_later, (void (*)(void *)) (sds_delete));
    }
    return 0;
}

void db_clear(redis_server_t* server) {
    /* 清空所有 db：先遍历每个 dict 收集所有 key，再统一删除，避免在迭代中删 key 导致问题 */
    for (int dbid = 0; dbid < server->db_num; dbid++) {
        redis_db_t* db = server->dbs + dbid;
        if (!db || !db->keys) continue;
        LATTE_LIB_LOG(LOG_DEBUG, "load_command: clearing db %d, num_dicts=%lld", dbid, (long long)db->keys->num_dicts);

        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) continue;
            LATTE_LIB_LOG(LOG_DEBUG, "load_command: db %d dict %lld collecting keys to delete", dbid, (long long)didx);
            /* 第一遍：迭代 dict 收集所有 key（复制 sds_dup），兼容 key|1 等存储方式 */
            sds* keys_to_delete = NULL;
            size_t key_count = 0;
            size_t key_capacity = 0;
            
            latte_iterator_t* it = dict_get_latte_iterator(d);
            if (!it) {
                LATTE_LIB_LOG(LOG_DEBUG, "load_command: db %d dict %lld iterator is NULL", dbid, (long long)didx);
                continue;
            }
            /* Use safe iterator so delete/release does not assert fingerprint (we delete keys after iteration). */
            // ((dict_iterator_t*)it)->safe = 1;
            LATTE_LIB_LOG(LOG_DEBUG, "db clear: db %d dict %lld iterator created it=%p", dbid, (long long)didx, (void*)it);
            while (latte_iterator_has_next(it)) {
                latte_pair_t* pair = (latte_pair_t*)latte_iterator_next(it);
                if (!pair) break;
                sds key = (sds)latte_pair_key(pair);
                /* 兼容 dict 中 (key|1) 等特殊 entry，解码得到真实 key 指针 */
                // if (key && ((uintptr_t)key & 1)) key = (sds)((uintptr_t)key & ~(uintptr_t)1);
                LATTE_LIB_LOG(LOG_DEBUG, "db clear: db %d dict %lld iter key_count=%zu key=%s", dbid, (long long)didx, key_count, key);
                if (key) {
                    if (key_count >= key_capacity) {
                        size_t new_capacity = key_capacity ? key_capacity * 2 : 16;
                        sds* new_keys = zrealloc(keys_to_delete, new_capacity * sizeof(sds));
                        if (!new_keys) {
                            /* Out of memory - free what we have and break */
                            for (size_t i = 0; i < key_count; i++) {
                                sds_delete(keys_to_delete[i]);
                            }
                            zfree(keys_to_delete);
                            latte_iterator_delete(it);
                            break;
                        }
                        keys_to_delete = new_keys;
                        key_capacity = new_capacity;
                    }
                    keys_to_delete[key_count++] = sds_dup(key);
                }
            }
            latte_iterator_delete(it);

            /* 第二遍：按收集到的 key 调用 db_delete_key 删除，并释放本地的 sds 副本 */
            LATTE_LIB_LOG(LOG_DEBUG, "db clear: db %d dict %lld deleting %zu keys", dbid, (long long)didx, key_count);
            for (size_t i = 0; i < key_count; i++) {
                db_delete_key(server, db, keys_to_delete[i]);
                sds_delete(keys_to_delete[i]);
            }
            if (keys_to_delete) zfree(keys_to_delete);
        }
    }
    
}