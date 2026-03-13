#include "ebuckets.h"
#include "zmalloc/zmalloc.h"

#define EB_LIST_MAX_ITEMS 16
eb_bucket_t* eb_bucket_new(eb_bucket_func* func) {
    eb_bucket_t* bucket = zmalloc(sizeof(eb_bucket_t));
    bucket->func = func;
    return bucket;
}

void eb_bucket_delete(eb_bucket_t* bucket) {
    zfree(bucket);
}

int eb_bucket_is_list(eb_bucket_t* bucket) {
    return bucket->ctx == NULL? 0 : (((uintptr_t)(void*)bucket->ctx & 0x1) == 1);
}

int eb_bucket_is_null(eb_bucket_t* bucket) {
    return bucket->ctx == NULL;
}

static inline uint64_t eb_get_meta_time(eb_bucket_meta_t* meta) {
    return ((uint64_t)meta->time_hi << 32) | meta->time_lo;
}

/* set lsb in ebuckets pointer to 1 to mark it as list. Unless empty (NULL) */
static inline void* eb_mark_as_list(void* item) {
    if (item == NULL) return item;

    /* either 'itemsAddrAreOdd' or not, we end up with lsb is set to 1 */
    return (void *) ((uintptr_t) item | 1);
}

static inline void* eb_get_list_ptr(void* item) {
    return (void *) ((uintptr_t) item & ~1);
}

int eb_bucket_add_node_to_list(eb_bucket_t* bucket, void* item, uint64_t expireTime) {
    eb_bucket_meta_t* meta = bucket->func->get_meta(item);
    if (meta == NULL) {
        return 0;
    }
    if (eb_bucket_is_null(bucket->ctx)) {
        meta->next = NULL;
        meta->first_item_bucket = 1;
        meta->last_item_bucket = 1;
        meta->num_items = 1;
        meta->last_in_segment = 1;
        bucket->ctx = eb_mark_as_list(item);
        return 0;
    }
    void* head = eb_get_list_ptr(bucket->ctx);
    eb_bucket_meta_t* meta_head = bucket->func->get_meta(head);
    if (meta_head->numItems == EB_LIST_MAX_ITEMS) {
        return 1;
    }

    if (eb_get_meta_time(meta_head) > eb_get_meta_time(meta)) {
        meta->next = head;
        meta->first_item_bucket = 1;
        meta->num_items = meta_head->numItems + 1;
        meta_head->first_item_bucket = 0;
        meta_head->num_items = 0;
        bucket->ctx = eb_mark_as_list(item);
        return 0;
    }
    eb_bucket_meta_t* prev = meta_head;
    for (int i = 1; i < meta_head->num_items; i++) {
        void* next = prev->next;  
        eb_bucket_meta_t* meta_next = bucket->func->get_meta(next);
        if (eb_get_meta_time(meta_next) > eb_get_meta_time(meta)) { 
            meta_head->num_items +=1;
            meta->next = next; 
            prev->next = item;
            return 0;
        }
        prev = next;
    }
    /* insert item as thelast item of the list*/
    meta_head->num_items +=1;
    /* update last*/
    meta->next = NULL;
    meta->last_item_bucket = 1;
    meta->last_in_segment = 1;
    meta->last_in_segment = 1;
    /* update obsolete last item*/
    prev->last_in_segment = 0;
    prev->last_item_bucket = 0;
    prev->next = item;
    return 0;
}

typedef struct common_seg_hdr_t {
    void* head;
} common_seg_hdr_t;

typedef struct first_seg_hdr_t {
    void* head;             /* first item in the list */
    uint32_t total_items;   /* total items in the bucket, across chained segments */
    uint32_t num_segs;      /* number of segments in the bucket */
} first_seg_hdr_t;

typedef struct next_seg_hdr_t {
    void* head;            
    common_seg_hdr_t* prev_seg;   /* pointer to previous segment */
    first_seg_hdr_t* first_seg;   /* pointer to first segment of the bucket */
} next_seg_hdr_t;

typedef struct eb_bucket_rax_t {
    eb_bucket_rax_node_t *head;
    uint64_t numele;
    uint64_t numnodes;
    void* meta[];
} eb_bucket_rax_t;


#define EB_KEY_SIZE 6
/* Converts the logical starting time value of a given bucket-key to its equivalent
 * "physical" value in the context of an rax tree (rax-key). Although their values
 * are the same, their memory layouts differ. The raxKey layout orders bytes in
 * memory is from the MSB to the LSB, and the length of the key is EB_KEY_SIZE. */

static inline void bucket_key_to_rax_key(uint64_t time, unsigned char* rax_key) {
    for (int i = EB_KEY_SIZE - 1; i >= 0; i--) {
        rax_key[i] = (unsigned char)(time & 0xFF);
        time >>= 8;
    }
}
/* Converts the "physical" value of rax-key to its logical counterpart, representing
 * the starting time value of a bucket. The values are equivalent, but their memory
 * layouts differ. The raxKey is assumed to be ordered from the MSB to the LSB with
 * a length of EB_KEY_SIZE. The resulting bucket-key is the logical representation
 * with respect to ebuckets. */
static inline uint64_t rax_key_to_bucket_key(unsigned char* rax_key) {
    uint64_t time = 0;
    for (int i = 0; i < EB_KEY_SIZE; i++) {
        time = (time << 8) | rax_key[i];
    }
    return time;
}
static int eb_convert_list_to_rax(eb_bucket_t* bucket) {
    first_seg_hdr_t* first_seg_hdr = zmalloc(sizeof(first_seg_hdr_t));
    first_seg_hdr->head = bucket->ctx;
    
    first_seg_hdr->num_segs = 1;

    eb_bucket_meta_t* meta = bucket->func->get_meta(eb_get_list_ptr(bucket->ctx));
    first_seg_hdr->total_items = meta->num_items;

    uint64_t time = eb_get_meta_time(meta);
    /*  update last item to point on the segment header*/
    while (meta->last_item_bucket == 0) {
        meta = bucket->func->get_meta(meta->next);
    }
    meta->next = first_seg_hdr;

    unsigned char rax_key[EB_KEY_SIZE];
    bucket_key_to_rax_key(time, rax_key);
    eb_bucket_rax_t* rax = eb_bucket_rax_new_with_meta_data(sizeof(uint64_t));
    *rb_rax_num_items(rax) =  meta->num_items;
    rax_insert(rax, rax_key, EB_KEY_SIZE, first_seg_hdr, NULL);
    bucket->ctx = rax;
    return 1;
}

int eb_bucket_add_node(eb_bucket_t* bucket, void* item, uint64_t time) {
    int res = 0;
    eb_bucket_meta_t* meta = bucket->func->get_meta(item);
    if (meta == NULL) {
        return 0;
    }
    meta->time_lo = time & 0xFFFFFFFF;
    meta->time_hi = time >> 32;
    if (eb_bucket_is_list(bucket->ctx) || eb_bucket_is_null(bucket->ctx)) {
        if ((res = eb_bucket_add_node_to_list(bucket->ctx, meta, time)) == 1) {
            bucket->ctx = eb_convert_list_to_rax(bucket);
            res =eb_bucket_add_node_to_rax(bucket->ctx, item, time);
        }
        
    } else {
        res = eb_bucket_add_node_to_rax(bucket->ctx, item, time);
    }
    eb_validate_structure(bucket);
    return res;
}