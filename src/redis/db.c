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

/**
 * 获取指定key所在的字典分片索引
 * 输入: key - sds字符串类型的key
 * 返回: 字典索引(当前实现总是返回0，即单分片模式)
 * 注释: 在集群模式下，这里会根据key的哈希值计算对应的slot
 */
int get_kv_store_index_for_key(sds key) {
    return 0;
}


/**
 * 向数据库添加key-value对（外部接口）
 * 输入: server - redis服务器实例, db - 数据库, key - 键对象, value - 值对象
 * 返回: 0表示成功，非0表示失败
 * 注释: 这是对外的简化接口，不允许更新已存在的key
 */
int db_add_key_value(redis_server_t* server,redis_db_t* db, latte_object_t* key, latte_object_t* value) {
    return db_add_key_value_internal(server, db, key, value, 0);
}



/**
 * 原地更新已存在key的value值
 * 输入: server - redis服务器实例, db - 数据库, key - 键对象, val - 新值对象, overwrite - 是否允许覆盖, de - 字典条目(可为NULL)
 * 返回: 0表示成功
 * 注释: 关键特性是继承旧值的LRU信息(val->lru = old->lru)，保持访问时间连续性
 */
int db_set_value(redis_server_t* server,redis_db_t* db, latte_object_t* key, latte_object_t* val, int overwrite, dict_entry_t* de) {
    sds key_ptr;
    latte_assert_with_info(get_sds_from_object(key, &key_ptr) == 0, "key is not a string");
    int dict_index = get_kv_store_index_for_key(key_ptr);
    if (!de) de = kv_store_dict_find(db->keys, dict_index, key_ptr);
    latte_assert_with_info(de != NULL, "[db_set_value] kv_store dict unfind key %s", key_ptr);
    latte_object_t* old = dict_get_entry_val(de);
    val->lru = old->lru;  // 继承旧值的LRU时间戳，保持访问历史连续性

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
        latte_object_decr_ref_count(old);  // 释放旧值对象的引用计数
    // }
    return 0;
}


/**
 * 获取LFU算法使用的时间（以分钟为单位）
 * 输入: server - redis服务器实例
 * 返回: 当前时间的分钟数，取低16位(0-65535循环)
 * 注释: LFU使用分钟级时间戳，节省存储空间
 */
unsigned long lfu_get_time_in_minutes(redis_server_t* server) {
    return (server->unixtime/60) & 65535;
}
#define LRU_CLOCK_RESOLUTION 1000

/**
 * 获取LRU时钟值（高精度版本）
 * 输入: server - redis服务器实例
 * 返回: 当前LRU时钟值
 * 注释: 基于毫秒时间计算，精度为LRU_CLOCK_RESOLUTION(1000ms)
 */
unsigned int get_lru_clock(redis_server_t* server) {
    return (mstime() / LRU_CLOCK_RESOLUTION) & LRU_CLOCK_MAX;
}

/**
 * 获取当前LRU时钟值
 * 输入: server - redis服务器实例
 * 返回: LRU时钟值
 * 注释: 根据server频率选择使用缓存时钟或实时计算，优化性能
 */
unsigned int lru_clock(redis_server_t* server) {
    unsigned int lruclock;
    if (1000/server->hz <= LRU_CLOCK_RESOLUTION) {
        lruclock = server->lruclock;  // 使用服务器缓存的时钟值
    } else {
        lruclock = get_lru_clock(server);  // 实时计算时钟值
    }
    return lruclock;
}

#define LFU_INIT_VAL 5
/**
 * 根据服务器的内存淘汰策略初始化对象的LRU或LFU字段
 * 输入: server - redis服务器实例, o - 要初始化的对象
 * 返回: 无
 * 注释: 共享对象(refcount=INT_MAX)跳过初始化；LFU模式下使用(时间<<8)|初始值；LRU模式下使用当前时钟
 */
void init_object_lru_or_lfu(redis_server_t* server, latte_object_t* o) {
    if (o->refcount == OBJ_SHARED_REFCOUNT) return;  // 共享对象不需要LRU/LFU信息

    if (server->maxmemory_policy & MAXMEMORY_FLAG_LFU) {
        // LFU模式：高24位存储时间(分钟)，低8位存储访问频率计数器
        o->lru = (lfu_get_time_in_minutes(server) << 8) | LFU_INIT_VAL;
    } else {
        // LRU模式：存储当前时钟值作为最后访问时间
        o->lru = lru_clock(server);
    }
    return;
}

/**
 * 向数据库添加key-value对（内部实现）
 * 输入: server - redis服务器实例, db - 数据库, key - 键对象, val - 值对象, update_if_existing - 是否允许更新已存在的key
 * 返回: 0表示成功，非0表示失败
 * 注释: 处理key已存在的情况，新key会复制sds并初始化LRU/LFU信息
 */
int db_add_key_value_internal(redis_server_t* server,redis_db_t* db, latte_object_t* key, latte_object_t* val, int update_if_existing) {
    dict_entry_t *existing;
    sds key_ptr;
    latte_assert_with_info(get_sds_from_object(key, &key_ptr) == 0, "key is not a string");// "key is not a string"
    int dict_index = get_kv_store_index_for_key(key_ptr);
    sds new_key = sds_new(key->ptr);  // 创建key的副本，避免外部修改影响
    dict_entry_t* de = kv_store_dict_add_raw(db->keys, dict_index, new_key, &existing);
    if (existing) {
        sds_delete(new_key);  // key已存在，释放副本
        /* Key already exists: update in place (e.g. SET overwrite) to avoid assert crash */
        db_set_value(server, db, key, val, 1, existing);  // 原地更新已存在的key
        return 0;
    }
    if (de == NULL) {
        sds_delete(new_key);
        return -1;
    }
    init_object_lru_or_lfu(server, val);  // 为新对象初始化LRU/LFU信息
    kv_store_dict_set_val(db->keys, dict_index, de, val);
    // signal_key_as_ready(db, key, val->type);  // 通知key就绪（用于阻塞操作）
    // notify_keyspace_event(NOTIFY_NEW, "new", key, db->id);  // 发送keyspace事件通知
    return 0;
}


/* kv_store_t function */

/**
 * 获取指定索引的字典对象
 * 输入: kvs - kv存储结构, didx - 字典索引
 * 返回: 字典指针，可能为NULL(如果该分片未创建)
 */
dict_t* kv_store_get_dict(kv_store_t* kvs, int didx) {
    return kvs->dicts[didx];
}

#define dict_meta_data(d) (&(d)->metadata)
/**
 * 懒创建字典：如果指定索引的字典不存在则创建
 * 输入: kvs - kv存储结构, didx - 字典索引
 * 返回: 字典指针，保证非NULL
 * 注释: 延迟创建策略，只有在实际需要时才分配内存，避免空字典占用资源
 */
static dict_t* kv_store_create_dict_if_needed(kv_store_t* kvs, int didx) {
    dict_t *d = kv_store_get_dict(kvs, didx);
    if (d) return d;
    kvs->dicts[didx] = dict_new(kvs->dtype); //延迟创建
    kv_store_dict_meta_data_t* meta_data = (kv_store_dict_meta_data_t*)dict_meta_data(kvs->dicts[didx]);
    // meta_data->kvs = kvs;
    kvs->allocated_dicts++;  //可用字典个数增加（由于是分片存储，不能用vector因为需要随机访问）
    return kvs->dicts[didx];
}

/**
 * 在指定字典分片中查找key
 * 输入: kvs - kv存储结构, didx - 字典索引, key - 要查找的键
 * 返回: 字典条目指针，未找到则返回NULL
 * 注释: 如果字典分片不存在，直接返回NULL而不创建
 */
dict_entry_t* kv_store_dict_find(kv_store_t* kvs, int didx, void* key) {
    dict_t* d = kv_store_get_dict(kvs, didx);
    if (d == NULL) return NULL;
    return dict_find(d, key);
}

/**
 * 从指定字典分片中删除key
 * 输入: kvs - kv存储结构, didx - 字典索引, key - 要删除的键
 * 返回: DICT_OK表示成功，DICT_ERR表示失败
 * 注释: 删除成功后会更新Fenwick Tree的累计计数
 */
int kv_store_dict_delete_key(kv_store_t* kvs, int didx, const void* key) {
    dict_t* d = kv_store_get_dict(kvs, didx);
    if (d == NULL) return DICT_ERR;
    int ret = dict_delete_key(d, key);
    if (ret == DICT_OK)
        cumulative_key_count_add(kvs, didx, -1);  // 更新Fenwick Tree统计信息
    return ret;
}



/**
 * 使用Fenwick Tree(树状数组)更新累计key数量统计
 * 输入: kvs - kv存储结构, didx - 字典索引, delta - 变化量(正数为增加，负数为减少)
 * 返回: 无
 * 注释: 这是关键的统计算法！Fenwick Tree能在O(log n)时间内完成区间更新和前缀查询
 *       核心技巧是 idx & -idx，利用二进制最低位1找到下一个需要更新的父节点
 */
static void cumulative_key_count_add(kv_store_t* kvs, int didx, long delta) {
    kvs->key_count += delta; //总key数统计

    dict_t* d = kv_store_get_dict(kvs, didx);
    size_t dsize = dict_size(d);
    int non_empty_dicts_delta = dsize == 1 ? 1 : dsize == 0? -1:0; //从0到1则+1，从1到0则-1，其他情况为0
    kvs->non_empty_dicts += non_empty_dicts_delta;

    if (kvs->num_dicts == 1) return;  // 单字典模式无需Fenwick Tree

    int idx = didx + 1;  // Fenwick Tree使用1-based索引
    // Fenwick Tree核心更新算法：沿着二进制路径向上更新所有父节点
    while (idx <= kvs->num_dicts) {
        if (delta < 0) {
            latte_assert_with_info(kvs->dict_size_index[idx] >= (unsigned long long)labs(delta), "kvs->dict_size_index[idx] is less than labs(delta)");
        }
        // 更新当前节点的累计值
        kvs->dict_size_index[idx] += delta;
        // 关键算法：idx & -idx 获取最低位的1，用于找到下一个父节点
        // 例如：idx=6(0110) -> idx&-idx=2(0010) -> 下一个idx=8(1000)
        //      idx=4(0100) -> idx&-idx=4(0100) -> 下一个idx=8(1000)
        // 这样确保了每个节点都会正确更新其在树中负责的区间
        idx += (idx & -idx);
    }

}

/**
 * 向指定字典分片添加新的key-value条目(原始接口)
 * 输入: kvs - kv存储结构, didx - 字典索引, key - 键, existing - 输出参数，如果key已存在则指向现有条目
 * 返回: 新创建的字典条目指针，如果key已存在则返回NULL
 * 注释: 会自动创建不存在的字典分片，添加成功后更新Fenwick Tree统计
 */
dict_entry_t* kv_store_dict_add_raw(kv_store_t* kvs, int didx, void *key, dict_entry_t** existing) {
    dict_t* d = kv_store_create_dict_if_needed(kvs, didx);  // 按需创建字典
    dict_entry_t* ret = dict_add_raw(d, key, existing);
    if (ret) cumulative_key_count_add(kvs, didx, 1);  // 添加成功，更新统计
    return ret;
}

/**
 * 设置字典条目的值
 * 输入: kvs - kv存储结构, didx - 字典索引, de - 字典条目, val - 新值
 * 返回: 0表示成功
 */
int kv_store_dict_set_val(kv_store_t* kvs, int didx, dict_entry_t* de, void* val) {
    dict_t * d = kv_store_get_dict(kvs, didx);
    dict_set_val(d, de, val);
    return 0;
}

/* ---------- Expire helpers ---------- */

/**
 * 获取key的过期时间
 * 输入: db - 数据库, key - sds格式的键
 * 返回: Unix毫秒时间戳，0表示无过期时间
 */
long long db_get_expire(redis_db_t* db, sds key) {
    int didx = get_kv_store_index_for_key(key);
    dict_entry_t* de = kv_store_dict_find(db->expires, didx, key);
    if (!de) return 0;
    return (long long)(uintptr_t)dict_get_entry_val(de);  // 将指针值转换为时间戳
}

/**
 * 设置key的过期时间
 * 输入: server - redis服务器实例, db - 数据库, key - sds格式的键, when_ms - Unix毫秒时间戳
 * 返回: 0表示成功，非0表示失败
 * 注释: 过期时间存储在单独的expires字典中，value直接用指针存储时间戳值
 */
int db_set_expire(redis_server_t* server, redis_db_t* db, sds key, long long when_ms) {
    UNUSED(server);
    int didx = get_kv_store_index_for_key(key);
    dict_entry_t* existing = NULL;
    dict_entry_t* de = kv_store_dict_add_raw(db->expires, didx, key, &existing);
    if (!de) return -1;
    kv_store_dict_set_val(db->expires, didx, de, (void*)(uintptr_t)when_ms);  // 将时间戳作为指针值存储
    return 0;
}

/**
 * 移除key的过期时间设置
 * 输入: db - 数据库, key - sds格式的键
 * 返回: 无
 */
void db_remove_expire(redis_db_t* db, sds key) {
    int didx = get_kv_store_index_for_key(key);
    kv_store_dict_delete_key(db->expires, didx, key);
}

/**
 * 从数据库中删除key及其过期记录
 * 输入: server - redis服务器实例, db - 数据库, key - sds格式的键
 * 返回: 无
 * 注释: 同时删除主存储和过期时间存储中的记录
 */
void db_delete_key(redis_server_t* server, redis_db_t* db, sds key) {
    UNUSED(server);
    int didx = get_kv_store_index_for_key(key);
    kv_store_dict_delete_key(db->expires, didx, key);  // 先删除过期记录
    kv_store_dict_delete_key(db->keys, didx, key);     // 再删除主记录
}

/**
 * 检查key是否已过期，如果已过期则删除
 * 输入: server - redis服务器实例, db - 数据库, key - sds格式的键
 * 返回: 1表示key已过期并被删除，0表示key未过期或不存在
 * 注释: 惰性过期检查，只在访问时检查是否过期
 */
int expire_if_needed(redis_server_t* server, redis_db_t* db, sds key) {
    dict_entry_t* de = kv_store_dict_find(db->expires, get_kv_store_index_for_key(key), key);
    if (!de) return 0;  // 无过期时间
    long long when_ms = (long long)(uintptr_t)dict_get_entry_val(de);
    if (mstime() < when_ms) return 0;  // 未过期
    db_delete_key(server, db, key);  // 已过期，删除key
    return 1;
}

/**
 * 设置字典条目的键
 * 输入: kvs - kv存储结构, didx - 字典索引, de - 字典条目, key - 新键
 * 返回: 0表示成功
 */
int kv_store_dict_set_key(kv_store_t* kvs, int didx, dict_entry_t* de, void* key) {
    dict_t * d = kv_store_get_dict(kvs, didx);
    dict_set_key(d, de, key);
    return 0;
}

/**
 * 字典对象析构函数：释放latte_object_t对象的引用计数
 * 输入: d - 字典(未使用), val - 要释放的对象
 * 返回: 无
 * 注释: 用于keys字典，当删除条目时自动释放value对象的内存
 */
void dict_object_destructor(dict_t* d, void* val) {
    UNUSED(d);
    if (val == NULL) return;
    latte_object_decr_ref_count(val);  // 减少对象引用计数，可能触发内存释放
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


// keys字典的函数表：key=sds(在添加时复制), value=latte_object_t*
dict_func_t kv_store_keys_dict_type = {
    dict_sds_hash,                       // 哈希函数：计算sds字符串的哈希值
    NULL,                                // key复制函数：不复制，直接使用传入的sds
    NULL,                                // value复制函数：不复制，直接存储对象指针
    dict_sds_key_compare,               // key比较函数：比较两个sds字符串
    dict_sds_destructor,                // key析构函数：释放sds内存
    dict_object_destructor,             // value析构函数：减少对象引用计数
    dict_resize_allowed,                // 扩容判断函数：根据内存使用情况决定是否允许扩容
    // kv_store_dict_rehashing_started,  // rehash开始回调
    // kv_store_dict_rehashing_completed, // rehash完成回调
    kv_store_dict_meta_data_size,       // 元数据大小函数
    // .embed_key = dict_sds_embed_key,  // 内嵌key优化
    // .embedded_entry = 1,              // 支持内嵌条目
};

// expires字典的函数表：key=sds(复制), value=(void*)(uintptr_t)过期时间戳
dict_func_t kv_store_expires_dict_type = {
    dict_sds_hash,                       // 哈希函数：计算sds字符串的哈希值
    dict_sds_dup,                       // key复制函数：复制sds字符串
    NULL,                                // value复制函数：时间戳直接存储为指针值
    dict_sds_key_compare,               // key比较函数：比较两个sds字符串
    dict_sds_destructor,                // key析构函数：释放复制的sds内存
    NULL,                                // value析构函数：时间戳无需释放
    dict_resize_allowed,                // 扩容判断函数
    kv_store_dict_meta_data_size,       // 元数据大小函数
};




#define KVSTORE_ALLOCATE_DICTS_ON_DEMAND (1 << 0)
/**
 * 创建kv_store存储结构
 * 输入: type - 字典函数表, num_dicts_bits - 字典数量的位数, flags - 创建标志
 * 返回: 新创建的kv_store指针
 * 注释: 支持分片存储，最多支持2^16个字典分片。包含Fenwick Tree初始化用于快速统计
 */
kv_store_t *kv_store_create(dict_func_t *type, int num_dicts_bits, int flags) {
    /* 最多支持2^16个字典分片，因为要为dict cursor保留48位空间 */
    latte_assert_with_info(num_dicts_bits <= 16, "num_dicts_bits is too large");

    /* kvstore的字典类型需要使用特定的回调函数，未来如有变化需要修改 */
    // assert(type->rehashingStarted == kvstoreDictRehashingStarted);
    // assert(type->rehashingCompleted == kvstoreDictRehashingCompleted);
    latte_assert_with_info(type->dictEntryMetadataBytes == kv_store_dict_meta_data_size, "type->dictEntryMetadataBytes is not equal to kv_store_dict_meta_data_size");

    kv_store_t *kvs = zcalloc(sizeof(*kvs));
    kvs->dtype = type;
    kvs->flags = flags;

    kvs->num_dicts_bits = num_dicts_bits;
    kvs->num_dicts = 1 << kvs->num_dicts_bits;  // 计算字典总数 = 2^bits
    kvs->dicts = zcalloc(sizeof(dict_t *) * kvs->num_dicts);

    if (!(kvs->flags & KVSTORE_ALLOCATE_DICTS_ON_DEMAND)) {
        // 立即分配所有字典
        for (int i = 0; i < kvs->num_dicts; i++) {
            LATTE_LIB_LOG(LOG_INFO, "DB CREATE");
            kv_store_create_dict_if_needed(kvs, i);
        }
    } else {
        // 按需分配模式：初始化为NULL，使用时再创建
        for (int i = 0; i < kvs->num_dicts; i++) {
            kvs->dicts[i] = NULL;
        }
    }

    kvs->rehashing = list_new();                 // 创建rehash队列
    kvs->key_count = 0;                          // 初始化key总数
    kvs->non_empty_dicts = 0;                    // 初始化非空字典数
    kvs->resize_cursor = 0;                      // 初始化调整大小游标
    // 为多字典模式分配Fenwick Tree数组，用于快速查询累计key数量
    kvs->dict_size_index = kvs->num_dicts > 1 ? zcalloc(sizeof(unsigned long long) * (kvs->num_dicts + 1)) : NULL;
    kvs->bucket_count = 0;                       // 初始化bucket总数
    kvs->overhead_hashtable_lut = 0;            // 初始化哈希表查找开销
    kvs->overhead_hashtable_rehashing = 0;       // 初始化rehash开销

    return kvs;
}

/**
 * 计算对象的哈希值（用于字典存储）
 * 输入: key - latte_object_t对象指针
 * 返回: 64位哈希值
 * 注释: 直接使用对象内部的sds字符串计算哈希
 */
uint64_t dict_object_hash(const void *key) {
    const latte_object_t *o = key;
    return dict_gen_hash_function(o->ptr, sds_len((sds)o->ptr));
}

/**
 * 比较两个对象的键是否相等
 * 输入: d - 字典(未使用), key1/key2 - 要比较的latte_object_t对象
 * 返回: 0表示相等，非0表示不等
 * 注释: 委托给sds字符串比较函数
 */
int dict_object_key_compare(dict_t *d, const void *key1, const void *key2) {
    const latte_object_t *o1 = key1, *o2 = key2;
    return dict_sds_key_compare(d, o1->ptr, o2->ptr);
}

/**
 * 链表析构函数：释放list_t对象
 * 输入: d - 字典(未使用), val - 要释放的链表
 * 返回: 无
 * 注释: 用于blocking_keys等字典，value是链表类型
 */
void dict_list_destructor(dict_t *d, void *val) {
    UNUSED(d);
    list_delete((list_t *)val);
}

// 用于blocking_keys字典的函数表
dict_func_t key_list_dict_type = {
    dict_object_hash,                    // 对象哈希函数
    NULL,                                // 不复制key
    NULL,                                // 不复制value
    dict_object_key_compare,            // 对象比较函数
    dict_object_destructor,             // 释放对象
    dict_list_destructor,               // 释放链表
    NULL                                 // 无扩容限制
};


/**
 * 计算编码对象的哈希值
 * 输入: key - latte_object_t对象指针
 * 返回: 64位哈希值
 * 注释: 在新的object_manager系统中，所有字符串对象都是sds编码
 */
uint64_t dict_encode_object_hash(const void *key) {
    latte_object_t *o = (latte_object_t *)key;

    /* 在新的 object_manager 系统中，所有字符串对象都是 sds 编码 */
    if (sds_encoded_object(o)) {
        return dict_gen_hash_function(o->ptr, sds_len((sds)o->ptr));
    } else {
        latte_panic("Unknown string encoding");  // 不支持的编码类型
    }
}



/**
 * 比较两个编码对象的键是否相等
 * 输入: d - 字典, key1/key2 - 要比较的latte_object_t对象
 * 返回: 0表示相等，非0表示不等
 * 注释: 处理静态引用计数对象，避免无效的引用计数操作
 */
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


// 用于ready_keys等字典的函数表：key=对象指针，value=指针值
dict_func_t object_key_pointer_value_dict_type = {
    dict_encode_object_hash,            // 编码对象哈希函数
    NULL,                                // 不复制key
    NULL,                                // 不复制value
    dict_encode_object_key_compare,     // 编码对象比较函数
    dict_object_destructor,             // 释放对象
    NULL,                                // 指针值无需释放
    NULL                                 // 无扩容限制
};

/**
 * 初始化Redis服务器的所有数据库
 * 输入: redis_server - redis服务器实例
 * 返回: 0表示成功
 * 注释: 为每个数据库创建keys/expires存储和各种辅助字典（阻塞、监视等）
 */
int init_redis_server_dbs(redis_server_t* redis_server) {
    int slot_count_bits = 0;  // 单分片模式（cluster模式下会增加分片数）
    int flags = KVSTORE_ALLOCATE_DICTS_ON_DEMAND;  // 按需分配字典，节省内存
    redis_server->dbs = zmalloc(sizeof(redis_db_t) * redis_server->db_num);
    for (int i = 0; i < redis_server->db_num; i++) {
        redis_server->dbs[i].id = i;
        // 创建主要的key-value存储
        redis_server->dbs[i].keys = kv_store_create(&kv_store_keys_dict_type, slot_count_bits, flags);
        // 创建过期时间存储
        redis_server->dbs[i].expires = kv_store_create(&kv_store_expires_dict_type, slot_count_bits, flags);
        redis_server->dbs[i].expires_cursor = 0;
        // 创建阻塞操作相关字典：key -> 等待该key的客户端列表
        redis_server->dbs[i].blocking_keys = dict_new(&key_list_dict_type);
        // 创建"key不存在时解除阻塞"字典：用于BLPOP等命令
        redis_server->dbs[i].blocking_keys_unblock_on_nokey = dict_new(&object_key_pointer_value_dict_type);
        // 创建就绪key字典：key已就绪，可以解除阻塞的客户端
        redis_server->dbs[i].ready_keys = dict_new(&object_key_pointer_value_dict_type);
        // 创建监视key字典：用于WATCH命令的事务支持
        redis_server->dbs[i].watched_keys = dict_new(&key_list_dict_type);
        redis_server->dbs[i].avg_ttl = 0;  // 平均TTL统计
        // 创建延迟内存碎片整理列表
        redis_server->dbs[i].defrag_later = list_new();
        list_set_free_method(redis_server->dbs[i].defrag_later, (void (*)(void *)) (sds_delete));
    }
    return 0;
}

/**
 * 清空所有数据库的内容
 * 输入: server - redis服务器实例
 * 返回: 无
 * 注释: 采用两遍遍历策略：第一遍收集所有key，第二遍统一删除，避免在迭代中修改字典导致的问题
 */
void db_clear(redis_server_t* server) {
    /* 清空所有 db：先遍历每个 dict 收集所有 key，再统一删除，避免在迭代中删 key 导致问题 */
    for (int dbid = 0; dbid < server->db_num; dbid++) {
        redis_db_t* db = server->dbs + dbid;
        if (!db || !db->keys) continue;
        LATTE_LIB_LOG(LOG_DEBUG, "load_command: clearing db %d, num_dicts=%lld", dbid, (long long)db->keys->num_dicts);

        // 遍历每个字典分片
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) continue;
            LATTE_LIB_LOG(LOG_DEBUG, "load_command: db %d dict %lld collecting keys to delete", dbid, (long long)didx);
            /* 第一遍：迭代 dict 收集所有 key（复制 sds_dup），兼容 key|1 等存储方式 */
            sds* keys_to_delete = NULL;  // 动态数组存储要删除的key
            size_t key_count = 0;        // 已收集的key数量
            size_t key_capacity = 0;     // 数组容量

            latte_iterator_t* it = dict_get_latte_iterator(d);
            if (!it) {
                LATTE_LIB_LOG(LOG_DEBUG, "load_command: db %d dict %lld iterator is NULL", dbid, (long long)didx);
                continue;
            }
            /* Use safe iterator so delete/release does not assert fingerprint (we delete keys after iteration). */
            // ((dict_iterator_t*)it)->safe = 1;  // 安全迭代器，允许在迭代后删除
            LATTE_LIB_LOG(LOG_DEBUG, "db clear: db %d dict %lld iterator created it=%p", dbid, (long long)didx, (void*)it);

            // 遍历字典收集所有key
            while (latte_iterator_has_next(it)) {
                latte_pair_t* pair = (latte_pair_t*)latte_iterator_next(it);
                if (!pair) break;
                sds key = (sds)latte_pair_key(pair);
                /* 兼容 dict 中 (key|1) 等特殊 entry，解码得到真实 key 指针 */
                // if (key && ((uintptr_t)key & 1)) key = (sds)((uintptr_t)key & ~(uintptr_t)1);
                LATTE_LIB_LOG(LOG_DEBUG, "db clear: db %d dict %lld iter key_count=%zu key=%s", dbid, (long long)didx, key_count, key);
                if (key) {
                    // 动态扩容数组
                    if (key_count >= key_capacity) {
                        size_t new_capacity = key_capacity ? key_capacity * 2 : 16;
                        sds* new_keys = zrealloc(keys_to_delete, new_capacity * sizeof(sds));
                        if (!new_keys) {
                            /* 内存不足 - 释放已收集的key并退出 */
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
                    keys_to_delete[key_count++] = sds_dup(key);  // 复制key，避免迭代中修改
                }
            }
            latte_iterator_delete(it);

            /* 第二遍：按收集到的 key 调用 db_delete_key 删除，并释放本地的 sds 副本 */
            LATTE_LIB_LOG(LOG_DEBUG, "db clear: db %d dict %lld deleting %zu keys", dbid, (long long)didx, key_count);
            for (size_t i = 0; i < key_count; i++) {
                db_delete_key(server, db, keys_to_delete[i]);  // 删除key及其过期记录
                sds_delete(keys_to_delete[i]);                 // 释放复制的key
            }
            if (keys_to_delete) zfree(keys_to_delete);
        }
    }

}