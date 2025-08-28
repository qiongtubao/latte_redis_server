#include "slowlog.h"
#include "object/string.h"

#define SLOWLOG_ENTRY_MAX_ARGC 32
#define SLOWLOG_ENTRY_MAX_STRING 128

static slowlog_entry_t* slowlog_entry_new(long long id, 
    redis_client_t* client) {
    slowlog_entry_t* entry = zmalloc(sizeof(slowlog_entry_t));
    entry->id = id;
    entry->time = ustime();
    entry->all_duration = client->client.end_time - client->client.start_time;
    entry->decode_duration = client->current_decode_time;
    entry->encode_duration = client->current_encode_time;
    entry->call_duration = client->current_call_time;
    entry->command = sds_new(client->lastcmd->name);

    int argc = client->argc;
    if (argc > SLOWLOG_ENTRY_MAX_ARGC) {
        argc = SLOWLOG_ENTRY_MAX_ARGC;
    } 
    entry->argv = zmalloc(sizeof(latte_object_t*) * argc);
    for (int j = 0; j < argc; j++) {
        if (client->argc != argc && j == argc-1) {
            entry->argv[j] = latte_object_new(OBJ_STRING,
                sds_cat_printf(sds_empty(),"... (%d more arguments)",
                argc-argc+1));
        } else {
            /* Trim too long strings as well... */
            if (client->argv[j]->type == OBJ_STRING &&
                sds_encoded_object(client->argv[j]) &&
                sds_len(client->argv[j]->ptr) > SLOWLOG_ENTRY_MAX_STRING)
            {
                sds s = sds_new_len(client->argv[j]->ptr, SLOWLOG_ENTRY_MAX_STRING);

                s = sds_cat_printf(s,"... (%lu more bytes)",
                    (unsigned long)
                    sds_len(client->argv[j]->ptr) - SLOWLOG_ENTRY_MAX_STRING);
                entry->argv[j] = latte_object_new(OBJ_STRING,s);
            } else if (client->argv[j]->refcount == OBJ_SHARED_REFCOUNT) {
                entry->argv[j] = client->argv[j];
            } else {
                /* Here we need to duplicate the string objects composing the
                 * argument vector of the command, because those may otherwise
                 * end shared with string objects stored into keys. Having
                 * shared objects between any part of Redis, and the data
                 * structure holding the data, is a problem: FLUSHALL ASYNC
                 * may release the shared string object and create a race. */
                entry->argv[j] = dup_string_object(client->argv[j]);
            }
        }
    }
    entry->argc = argc;
    entry->client_name = client_get_name(client) ? sds_dup(client_get_name(client)) : NULL;
    entry->client_ip = sds_new(client_get_peer_id(client));
    return entry;
}

void slowlog_entry_delete(slowlog_entry_t* entry) {
    sds_delete(entry->client_ip);
    sds_delete(entry->client_name);
    for (int i = 0; i < entry->argc; i++) {
        latte_object_decr_ref_count(entry->argv[i]);
    }
    zfree(entry->argv);
    zfree(entry->command);
    zfree(entry);
}

slowlog_manager_t* slowlog_manager_new(long long max_len, long long time_limit_us) {
    slowlog_manager_t* manager = zmalloc(sizeof(slowlog_manager_t));
    manager->entries = list_new();
    manager->max_len = max_len;
    manager->next_id = 0L;
    manager->time_limit_us = time_limit_us;
    manager->entries->free = (void (*)(void*))slowlog_entry_delete;
    return manager;
}

void slowlog_manager_delete(slowlog_manager_t* manager) {       
    list_delete(manager->entries);
    zfree(manager);
}

void slowlog_manager_set_time_limit_us(slowlog_manager_t* manager, long long time_limit_us) {
    manager->time_limit_us = time_limit_us;
}

void slowlog_manager_set_max_len(slowlog_manager_t* manager, long long max_len) {
    manager->max_len = max_len;
    while (list_length(manager->entries) > manager->max_len) {
        list_del_node(manager->entries, list_last(manager->entries));
    }
}

void slowlog_manager_push_if_needed(slowlog_manager_t* manager, redis_client_t* client) {
    if (manager->max_len < 0 || manager->time_limit_us < 0) {
        return;
    }
    if (client->client.end_time - client->client.start_time > manager->time_limit_us) {
        slowlog_entry_t* entry = slowlog_entry_new(
            manager->next_id++, 
            client);
        list_add_node_tail(manager->entries, entry);
    }

    while (list_length(manager->entries) > manager->max_len) {
        list_del_node(manager->entries, list_last(manager->entries));
    }
}