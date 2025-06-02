


#include "server.h"
#include <math.h>
#include "evict.h"
#include "utils/utils.h"

static struct eviction_pool_entry_t *eviction_pool_lru;
/**
 * @brief 
 *  池大小
 */
#define EVPOOL_SIZE 16
/**
 * @brief 
 *  字符串大小
 */
#define EVPOOL_CACHED_SDS_SIZE 255

/** 创建pool */
void eviction_pool_alloc(void) {
    struct eviction_pool_entry_t* ep;
    int j;

    ep = zmalloc(sizeof(*ep)*EVPOOL_SIZE);
    for (j = 0; j < EVPOOL_SIZE; j++) {
        ep[j].idle = 0;
        ep[j].key = NULL;
        ep[j].cached = sds_new_len(NULL, EVPOOL_CACHED_SDS_SIZE);
        ep[j].dbid = 0;
    }
    eviction_pool_lru = ep;
}

#define LRU_CLOCK_RESOLUTION 1000

/* Return the LRU clock, based on the clock resolution. This is a time
 * in a reduced-bits format that can be used to set and check the
 * object->lru field of redisObject structures. */
unsigned int get_lru_clock(void) {
    return (mstime()/LRU_CLOCK_RESOLUTION) & LRU_CLOCK_MAX;
}
/* This function is used to obtain the current LRU clock.
 * If the current resolution is lower than the frequency we refresh the
 * LRU clock (as it should be in production servers) we return the
 * precomputed value, otherwise we need to resort to a system call. */
unsigned int lru_clock(void) {
    unsigned int lruclock;
    if (1000/server.hz <= LRU_CLOCK_RESOLUTION) {
        latte_atomic_get(server.lruclock,lruclock);
    } else {
        lruclock = get_lru_clock();
    }
    return lruclock;
}


/* Given an object returns the min number of milliseconds the object was never
 * requested, using an approximated LRU algorithm. */
unsigned long long estimate_object_idle_time(robj *o) {
    unsigned long long lruclock = lru_clock();
    if (lruclock >= o->lru) {
        return (lruclock - o->lru) * LRU_CLOCK_RESOLUTION;
    } else {
        return (lruclock + (LRU_CLOCK_MAX - o->lru)) *
                    LRU_CLOCK_RESOLUTION;
    }
}

int eviction_pool_populate(latte_server_t* rs,int dbid, dict_t* sampledict, dict_t* key_dict, struct eviction_pool_entry_t *pool) {
    int j, k, count;
    dict_entry_t *samples[5];
    //获得多个key
    count = dict_get_some_keys(sampledict, samples, 5);
    for (j = 0; j < count; j++) {
        unsigned long long idle;
        sds key;
        latte_object_t* o;
        dict_entry_t* de;
        de = samples[j];
        key = dict_get_key(de);

        if (rs.maxmemory_policy != MAXMEMORY_VOLATILE_TTL) {
            //从key的字典里重新读取
            if (sampledict != key_dict) de = dict_find(key_dict, key);
            o = dict_get_val(de);
        }

        //计算分数
        if (rs.maxmemory_policy & MAXMEMORY_FLAG_LRU) {
            idle = estimate_object_idle_time(o);
        } else if ((rs.maxmemory_policy & MAXMEMORY_FLAG_LFU)) {
            idle = 255 - LFUDecrAndReturn(o);
        } else if (rs.maxmemory_policy == MAXMEMORY_VOLATILE_TTL) {
            idle = ULLONG_MAX - (long)dict_get_val(de);
        } else {
            redis_panic("Unknow eviction policy in eviction_pool_populate()");
        }

        k = 0;
        while (k < EVPOOL_SIZE &&
            pool[k].key && 
            pool[k].idle < idle) k++;
        
        if (k == 0 && pool[EVPOOL_SIZE-1].key != NULL) {
            //分数最大 淘汰
            continue;
        }  else if (k < EVPOOL_SIZE && pool[k].key == NULL) {

        } else {
            /* 插入中间位置，需要移动后面的元素向右 */
            if (pool[EVPOOL_SIZE - 1].key == NULL) {
                /* 在覆盖之前保存 SDS 缓存 */
                sds cached = pool[EVPOOL_SIZE-1].cached;
                /* 右边有空位，整体右移一位 */
                memmove(pool + k + 1, pool + k, sizeof(pool[0]) * (EVPOOL_SIZE -k -1));
                pool[k].cached = cached;
            } else {
                /* 没有空位了？那我们就把位置减一（k--），插入到前面 */
                k--;
                /* 把从左边开始到 k 的所有元素左移一位，
                这样会丢弃掉 idle 最小的那个元素（最差的候选） */

                /* 同样，在覆盖前保存 SDS 缓存 */
                sds cached = pool[0].cached;
                if (pool[0].key != pool[0].cached) sds_delete(pool[0].key); // 如果 key 不是指向缓存，则需要释放内存
                memmove(pool, pool+1,sizeof(pool[0]) * k);
                pool[k].cached = cached;
            }
            
        }
        /* 尝试复用缓存的 SDS 字符串以减少内存分配开销 */
        int klen = sds_len(key);
        if (klen > EVPOOL_CACHED_SDS_SIZE) {
            pool[k].key = sds_dup(key); // 键太长就单独复制一份
        } else {
            memcpy(pool[k].cached, key, klen +1); // 否则直接复制到缓存区域
            sds_set_len(pool[k].cached, klen);
            pool[k].key = pool[k].cached;
        }
        pool[k].idle = idle; // 设置 idle 分数
        pool[k].dbid = dbid; // 设置所属数据库编号
    }
}

int performEvictions(void) {
    
}