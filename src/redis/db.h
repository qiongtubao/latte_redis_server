
#ifndef __REDIS_DB_H
#define __REDIS_DB_H
#include "dict/dict.h"
#include "object/object.h"
#include "sds/sds.h"
#include "./server.h"


typedef struct kv_store_t {
    int flags;
    dict_func_t* dtype; //扩容时 创建dict_t 使用
    dict_t** dicts; //对slot 分槽  （主要针对cluster enabled）
    long long num_dicts;
    long long num_dicts_bits;
    list_t* rehashing;
    int resize_cursor;
    int allocated_dicts; //字典创建的个数
    int non_empty_dicts;
    unsigned long long key_count; //总个数统计
    unsigned long long bucket_count;
    unsigned long long *dict_size_index;
    size_t overhead_hashtable_lut;
    size_t overhead_hashtable_rehashing;
} kv_store_t;

typedef struct kv_store_iterator_t {
    kv_store_t* kvs;
    long long didx;
    long long next_didx;
    dict_iterator_t* di;
} kv_store_iterator_t;
typedef struct kv_store_dict_iterator_t
{
    kv_store_t* kvs;
    long long didx;
    dict_iterator_t* di;
} kv_store_dict_iterator_t;

typedef struct {
    list_node_t* rehashing_node; /** rehash 时使用的list */
    kv_store_t* kvs;
} kv_store_dict_meta_data_t;

int get_kv_store_index_for_key(sds key);
dict_entry_t* kv_store_dict_add_raw(kv_store_t* kvs, int didx, void *key, dict_entry_t** existing);
int kv_store_dict_set_val(kv_store_t* kvs, int didx, dict_entry_t* de, void* val);
int kv_store_dict_set_key(kv_store_t* kvs, int didx, dict_entry_t* de, void* key);
dict_entry_t* kv_store_dict_find(kv_store_t* kvs, int didx, void* key);

typedef struct redis_db_t {
    kv_store_t *keys;
    kv_store_t *expires;
    dict_t *blocking_keys;
    dict_t *blocking_keys_unblock_on_nokey;
    dict_t *ready_keys;  
    dict_t *watched_keys;
    int id;
    long long avg_ttl;
    unsigned long expires_cursor;
    list_t *defrag_later;
} redis_db_t;

int db_add_key_value(struct redis_server_t* server, redis_db_t* db, latte_object_t* key, latte_object_t* value);
int db_add_key_value_internal(struct redis_server_t* server, redis_db_t* db, latte_object_t* key, latte_object_t* value, int update_if_existing);

#endif