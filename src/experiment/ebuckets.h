#ifndef __EB_BUCKETS_H__    
#define __EB_BUCKETS_H__

#include "dict/dict.h"
/* Handler to ebuckets DS. Pointer to a list, rax or NULL (empty DS). See also ebIsList(). */
// typedef void *eb_t;

/* Users of ebuckets will store `eItem` which is just a void pointer to their
 * element. In addition, eItem should embed the ExpireMeta struct and supply
 * getter function (see EbucketsType.getExpireMeta).
 */
typedef void *eb_item_t;

typedef struct eb_bucket_meta_t {
    uint32_t time_lo;
    uint16_t time_hi;
    unsigned int last_in_segment: 1;
    unsigned int first_item_bucket: 1;
    unsigned int last_item_bucket: 1;
    unsigned int num_items: 5;

    void *next;
} eb_bucket_meta_t;

typedef struct eb_bucket_func {
    eb_bucket_meta_t* (*get_meta)(void*);
    void (*on_delete_item)(eitem_t* item, void* ctx);
    unsigned int itemsAddrAreOdd;
} eb_bucket_func;



typedef struct eb_bucket_t {
    eb_bucket_func* func;
    void* ctx;
} eb_bucket_t;

typedef enum expire_action_enum {
    ACT_REMOVE_EXP_ITEM=0,      /* Remove the item from ebuckets. */
    ACT_UPDATE_EXP_ITEM,        /* Re-insert the item with updated expiration-time.
                                   Before returning this value, the cb need to
                                   update expiration time of the item by assisting
                                   function ebSetMetaExpTime(). The item will be
                                   kept aside and will be added again to ebuckets
                                   at the end of ebExpire() */
    ACT_STOP_ACTIVE_EXP         /* Stop active-expiration. It will assume that
                                   provided 'item' wasn't deleted by the callback. */
} expire_action_enum;



eb_bucket_t* eb_bucket_new(eb_bucket_func* func);
void eb_bucket_delete(eb_bucket_t* bucket);
void eb_bucket_add_node(eb_bucket_t* bucket, void* item);
void eb_bucket_delete_node(eb_bucket_t* bucket, void* item);
latte_iterator_t* eb_bucket_get_iterator(eb_bucket_t* bucket);

#endif