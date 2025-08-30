
#ifndef __REDIS_EVICT_H
#define __REDIS_EVICT_H

struct eviction_pool_entry_t {
    unsigned long long idle;
    sds key;
    sds cached;
    int dbid;
    int slot;
} eviction_pool_entry_t;



/** lru  
 * Least Recently Used，最近最少使用
*/
#define MAXMEMORY_FLAG_LRU (1<<0)
/** lfu 
 * Least Frequently Used，最不经常使用
*/
#define MAXMEMORY_FLAG_LFU (1<<1)
/**
 * @brief 
 *  
 */
#define MAXMEMORY_FLAG_ALLKEYS (1<<2)
#define MAXMEMORY_FLAG_NO_SHARED_INTEGERS \
    (MAXMEMORY_FLAG_LRU|MAXMEMORY_FLAG_LFU)

#define MAXMEMORY_VOLATILE_LRU ((0<<8)|MAXMEMORY_FLAG_LRU)
#define MAXMEMORY_VOLATILE_LFU ((1<<8)|MAXMEMORY_FLAG_LFU)
#define MAXMEMORY_VOLATILE_TTL (2<<8)
#define MAXMEMORY_VOLATILE_RANDOM (3<<8)
#define MAXMEMORY_ALLKEYS_LRU ((4<<8)|MAXMEMORY_FLAG_LRU|MAXMEMORY_FLAG_ALLKEYS)
#define MAXMEMORY_ALLKEYS_LFU ((5<<8)|MAXMEMORY_FLAG_LFU|MAXMEMORY_FLAG_ALLKEYS)
#define MAXMEMORY_ALLKEYS_RANDOM ((6<<8)|MAXMEMORY_FLAG_ALLKEYS)
#define MAXMEMORY_NO_EVICTION (7<<8)


#endif