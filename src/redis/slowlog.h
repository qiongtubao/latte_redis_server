

#ifndef __REDIS_SLOWLOG_H
#define __REDIS_SLOWLOG_H

#include "client.h"

typedef struct slowlog_entry_t {
    long long id;
    ustime_t time;
    long long all_duration;
    long long decode_duration;
    long long encode_duration;
    long long call_duration; 
    sds command;
    sds client_name;
    sds client_ip;
    latte_object_t **argv;
    int argc;
} slowlog_entry_t;
// slowlog_entry_t* slowlog_entry_new(long long id, 
//     redis_client_t* client);
// void slowlog_entry_delete(slowlog_entry_t* entry);

typedef struct slowlog_manager_t {
    list_t* entries;
    long long max_len;
    long long time_limit_us;
    long long next_id;
} slowlog_manager_t;

slowlog_manager_t* slowlog_manager_new(long long max_len, long long time_limit_us);
void slowlog_manager_delete(slowlog_manager_t* manager);
void slowlog_manager_push_if_needed(slowlog_manager_t* manager, redis_client_t* client);



#endif