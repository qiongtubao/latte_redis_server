
#ifndef __REDIS_DB_H
#define __REDIS_DB_H
#include "dict/dict.h"
#include "object/object.h"
#include "sds/sds.h"
#include "./server.h"

struct redis_server_t;


typedef struct kv_store_t {
    int flags;                               // 存储标志位（如是否按需分配字典）
    dict_func_t* dtype;                      // 字典函数表，用于创建新字典时的操作函数集合
    dict_t** dicts;                          // 字典数组，用于分片存储（主要针对cluster模式）
    long long num_dicts;                     // 字典总数量（2^num_dicts_bits）
    long long num_dicts_bits;                // 字典数量的位数，用于计算分片索引
    list_t* rehashing;                       // 正在rehash的字典链表
    int resize_cursor;                       // 调整大小时的游标位置
    int allocated_dicts;                     // 已分配（创建）的字典个数
    int non_empty_dicts;                     // 非空字典的个数
    unsigned long long key_count;            // 所有字典中的key总数统计
    unsigned long long bucket_count;         // 所有字典中的bucket总数
    unsigned long long *dict_size_index;     // Fenwick Tree（树状数组），用于快速查询累计key数量
    size_t overhead_hashtable_lut;           // 哈希表查找表的内存开销
    size_t overhead_hashtable_rehashing;     // rehash过程中的内存开销
} kv_store_t;

typedef struct kv_store_iterator_t {
    kv_store_t* kvs;                         // 指向被迭代的kv_store
    long long didx;                          // 当前迭代的字典索引
    long long next_didx;                     // 下一个要迭代的字典索引
    dict_iterator_t* di;                     // 当前字典的迭代器
} kv_store_iterator_t;
typedef struct kv_store_dict_iterator_t
{
    kv_store_t* kvs;                         // 指向被迭代的kv_store
    long long didx;                          // 正在迭代的字典索引
    dict_iterator_t* di;                     // 字典迭代器
} kv_store_dict_iterator_t;

typedef struct {
    list_node_t* rehashing_node;             // rehash过程中使用的链表节点
    kv_store_t* kvs;                         // 指向所属的kv_store
} kv_store_dict_meta_data_t;

/**
 * 获取指定key所在的字典分片索引
 * 输入: key - sds字符串类型的key
 * 返回: 字典索引(当前实现总是返回0，即单分片模式)
 */
int get_kv_store_index_for_key(sds key);

/**
 * 获取指定索引的字典对象
 * 输入: kvs - kv存储结构, didx - 字典索引
 * 返回: 字典指针，可能为NULL(如果该分片未创建)
 */
dict_t* kv_store_get_dict(kv_store_t* kvs, int didx);

/**
 * 向指定字典分片添加新的key-value条目(原始接口)
 * 输入: kvs - kv存储结构, didx - 字典索引, key - 键, existing - 输出参数，如果key已存在则指向现有条目
 * 返回: 新创建的字典条目指针，如果key已存在则返回NULL
 */
dict_entry_t* kv_store_dict_add_raw(kv_store_t* kvs, int didx, void *key, dict_entry_t** existing);

/**
 * 设置字典条目的值
 * 输入: kvs - kv存储结构, didx - 字典索引, de - 字典条目, val - 新值
 * 返回: 0表示成功
 */
int kv_store_dict_set_val(kv_store_t* kvs, int didx, dict_entry_t* de, void* val);

/**
 * 设置字典条目的键
 * 输入: kvs - kv存储结构, didx - 字典索引, de - 字典条目, key - 新键
 * 返回: 0表示成功
 */
int kv_store_dict_set_key(kv_store_t* kvs, int didx, dict_entry_t* de, void* key);

/**
 * 在指定字典分片中查找key对应的条目
 * 输入: kvs - kv存储结构, didx - 字典索引, key - 要查找的键
 * 返回: 字典条目指针，未找到则返回NULL
 */
dict_entry_t* kv_store_dict_find(kv_store_t* kvs, int didx, void* key);

/**
 * 从指定字典分片中删除key
 * 输入: kvs - kv存储结构, didx - 字典索引, key - 要删除的键
 * 返回: DICT_OK表示成功，DICT_ERR表示失败
 */
int kv_store_dict_delete_key(kv_store_t* kvs, int didx, const void* key);

typedef struct redis_db_t {
    kv_store_t *keys;                        // 主要的key-value存储
    kv_store_t *expires;                     // 过期时间存储，key相同，value为过期时间戳
    dict_t *blocking_keys;                   // 阻塞操作等待的key列表
    dict_t *blocking_keys_unblock_on_nokey;  // 当key不存在时解除阻塞的key列表
    dict_t *ready_keys;                      // 已就绪可以解除阻塞的key列表
    dict_t *watched_keys;                    // 被WATCH命令监视的key列表（用于事务）
    int id;                                  // 数据库ID编号
    long long avg_ttl;                       // 平均TTL时间，用于统计
    unsigned long expires_cursor;            // 过期key扫描的游标位置
    list_t *defrag_later;                    // 延迟进行内存碎片整理的key列表
} redis_db_t;

/**
 * 向数据库添加key-value对（外部接口）
 * 输入: server - redis服务器实例, db - 数据库, key - 键对象, value - 值对象
 * 返回: 0表示成功，非0表示失败
 */
int db_add_key_value(struct redis_server_t* server, redis_db_t* db, latte_object_t* key, latte_object_t* value);

/**
 * 向数据库添加key-value对（内部接口，支持更新已存在的key）
 * 输入: server - redis服务器实例, db - 数据库, key - 键对象, value - 值对象, update_if_existing - 是否允许更新已存在的key
 * 返回: 0表示成功，非0表示失败
 */
int db_add_key_value_internal(struct redis_server_t* server, redis_db_t* db, latte_object_t* key, latte_object_t* value, int update_if_existing);

/**
 * 获取key的过期时间
 * 输入: db - 数据库, key - sds格式的键
 * 返回: Unix毫秒时间戳，0表示无过期时间
 */
long long db_get_expire(redis_db_t* db, sds key);

/**
 * 设置key的过期时间
 * 输入: server - redis服务器实例, db - 数据库, key - sds格式的键, when_ms - Unix毫秒时间戳
 * 返回: 0表示成功，非0表示失败
 */
int db_set_expire(struct redis_server_t* server, redis_db_t* db, sds key, long long when_ms);

/**
 * 移除key的过期时间设置
 * 输入: db - 数据库, key - sds格式的键
 * 返回: 无
 */
void db_remove_expire(redis_db_t* db, sds key);

/**
 * 检查key是否已过期，如果已过期则删除
 * 输入: server - redis服务器实例, db - 数据库, key - sds格式的键
 * 返回: 1表示key已过期并被删除，0表示key未过期或不存在
 */
int expire_if_needed(struct redis_server_t* server, redis_db_t* db, sds key);

/**
 * 从数据库中删除key及其过期记录
 * 输入: server - redis服务器实例, db - 数据库, key - sds格式的键
 * 返回: 无
 */
void db_delete_key(struct redis_server_t* server, redis_db_t* db, sds key);

/**
 * 清空所有数据库的内容
 * 输入: server - redis服务器实例
 * 返回: 无
 */
void db_clear(struct redis_server_t* server);


#endif