

#include "command_manager.h"
#include "zmalloc/zmalloc.h"
#include "dict/dict_plugins.h"
#include "../shared/shared.h"
#include "../redis/db.h"
#include "../redis/server.h"
#include "time/localtime.h"
#include "utils/utils.h"
#include "../object/string.h"
#include "../../deps/latte_c/src/odb/odb.h"
#include "../../deps/latte_c/src/object/object_manager.h"
#include "../../deps/latte_c/src/dict/dict.h"
#include "../../deps/latte_c/src/iterator/iterator.h"
#include "../../deps/latte_c/src/error/error.h"
#include <stdio.h>

struct acl_category_item_t acl_command_categories[] = {
    {"write", CMD_WRITE|CMD_CATEGORY_WRITE},
    {"read-only", CMD_READONLY|CMD_CATEGORY_READ},
    {"use-memory", CMD_DENYOOM},
    {"admin", CMD_ADMIN|CMD_CATEGORY_ADMIN|CMD_CATEGORY_DANGEROUS},
    {"pub-sub", CMD_PUBSUB|CMD_CATEGORY_PUBSUB},
    {"no-script", CMD_NOSCRIPT},
    {"random", CMD_RANDOM},
    {"to-sort", CMD_SORT_FOR_SCRIPT},
    {"ok-loading", CMD_LOADING},
    {"ok-stale", CMD_STALE},
    {"no-monitor", CMD_SKIP_MONITOR},
    {"no-slowlog", CMD_SKIP_SLOWLOG},
    {"cluster-asking", CMD_ASKING},
    {"fast", CMD_FAST | CMD_CATEGORY_FAST},
    {"no-auth", CMD_NO_AUTH},
    {"may-replicate", CMD_MAY_REPLICATE},
    {"@keyspace", CMD_CATEGORY_KEYSPACE},
    {"@read", CMD_CATEGORY_READ},
    {"@write", CMD_CATEGORY_WRITE},
    {"@set", CMD_CATEGORY_SET},
    {"@sortedset", CMD_CATEGORY_SORTEDSET},
    {"@list", CMD_CATEGORY_LIST},
    {"@hash", CMD_CATEGORY_HASH},
    {"@string", CMD_CATEGORY_STRING},
    {"@bitmap", CMD_CATEGORY_BITMAP},
    {"@hyperloglog", CMD_CATEGORY_HYPERLOGLOG},
    {"@geo", CMD_CATEGORY_GEO},
    {"@stream", CMD_CATEGORY_STREAM},
    {"@pubsub", CMD_CATEGORY_PUBSUB},
    {"@admin", CMD_CATEGORY_ADMIN},
    {"@fast", CMD_CATEGORY_FAST},
    {"@slow", CMD_CATEGORY_SLOW},
    {"@blocking", CMD_CATEGORY_BLOCKING},
    {"@dangerous", CMD_CATEGORY_DANGEROUS},
    {"@connection", CMD_CATEGORY_CONNECTION},
    {"@transaction", CMD_CATEGORY_TRANSACTION},
    {"@scripting", CMD_CATEGORY_SCRIPTING},
    /* swap */
    {"@swap_keyspace", CMD_SWAP_DATATYPE_KEYSPACE},
    {"@swap_string", CMD_SWAP_DATATYPE_STRING},
    {"@swap_hash", CMD_SWAP_DATATYPE_HASH},
    {"@swap_set", CMD_SWAP_DATATYPE_SET},
    {"@swap_zset", CMD_SWAP_DATATYPE_ZSET},
    {"@swap_list", CMD_SWAP_DATATYPE_LIST},
    {"@swap_bitmap", CMD_SWAP_DATATYPE_BITMAP},
    {NULL,0} /* Terminator. */
};


/* Given the category name the command returns the corresponding flag, or
 * zero if there is no match. */
uint64_t command_data_type_flag_by_name(const char *name) {
    for (int j = 0; acl_command_categories[j].flag != 0; j++) {
        if (!strcasecmp(name,acl_command_categories[j].name)) {
            return acl_command_categories[j].flag;
        }
    }
    return 0; /* No match. */
}

/* Parse the flags string description 'strflags' and set them to the
 * command 'c'. If the flags are all valid C_OK is returned, otherwise
 * C_ERR is returned (yet the recognized flags are set in the command). */
int populate_command_table_parse_flags(struct redis_command_t *c, char *strflags) {
    int argc;
    sds *argv;
    int catflag;
    /* Split the line into arguments for processing. */
    argv = sds_split_args(strflags,&argc);
    if (argv == NULL) return -1;

    for (int j = 0; j < argc; j++) {
        char *flag = argv[j];
        
        if((catflag = command_data_type_flag_by_name(flag)) != 0) {
            c->flags |= catflag;
        } else {
            sds_free_splitres(argv,argc);
            return -1;
        }
    }
    /* If it's not @fast is @slow in this binary world. */
    if (!(c->flags & CMD_CATEGORY_FAST)) c->flags |= CMD_CATEGORY_SLOW;

    sds_free_splitres(argv,argc);
    return 0;
}

struct redis_command_t* command_manager_lookup(command_manager_t* cm, sds command) {
    return dict_fetch_value(cm->commands, command);
}




#define USER_COMMAND_BITS_COUNT 1024
/* For ACL purposes, every user has a bitmap with the commands that such
 * user is allowed to execute. In order to populate the bitmap, every command
 * should have an assigned ID (that is used to index the bitmap). This function
 * creates such an ID: it uses sequential IDs, reusing the same ID for the same
 * command name, so that a command retains the same ID in case of modules that
 * are unloaded and later reloaded. */
unsigned long acl_get_command_id(command_manager_t* cm, const char *cmdname) {

    sds lowername = sds_new(cmdname);
    sds_to_lower(lowername);
    void *id;
    if (raxFind(cm->commandId,(unsigned char*)lowername,sds_len(lowername),&id)) {
        sds_delete(lowername);
        return (unsigned long)id;
    }
    raxInsert(cm->commandId,(unsigned char*)lowername,strlen(lowername),
              (void*)cm->nextid,NULL);
    sds_delete(lowername);
    unsigned long thisid = cm->nextid;
    cm->nextid++;

    /* We never assign the last bit in the user commands bitmap structure,
     * this way we can later check if this bit is set, understanding if the
     * current ACL for the user was created starting with a +@all to add all
     * the possible commands and just subtracting other single commands or
     * categories, or if, instead, the ACL was created just adding commands
     * and command categories from scratch, not allowing future commands by
     * default (loaded via modules). This is useful when rewriting the ACLs
     * with ACL SAVE. */
    if (cm->nextid == USER_COMMAND_BITS_COUNT-1) cm->nextid++;
    return thisid;
}

/*  ==================== commands ==================== */

void quit_command(redis_client_t* c) {
    add_reply(c, shared.ok);
    c->client.flags |= CLIENT_CLOSE_AFTER_REPLY;
}



void expire_command(redis_client_t* c) {
    redis_server_t* server = (redis_server_t*)c->client.server;
    redis_db_t* db = server->dbs + c->dbid;
    sds key_sds;
    long long seconds;

    if (c->argc != 3) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command", c->cmd->name);
        return;
    }
    if (string2ll(c->argv[2]->ptr, string_object_len(c->argv[2]), &seconds) == 0 || seconds < 0) {
        add_reply_error(c, "ERR value is not an integer or out of range");
        return;
    }
    key_sds = sds_new_len(c->argv[1]->ptr, string_object_len(c->argv[1]));
    if (kv_store_dict_find(db->keys, get_kv_store_index_for_key(key_sds), key_sds) == NULL) {
        sds_delete(key_sds);
        add_reply_long_long_with_prefix(c, 0, ':');
        return;
    }
    if (db_set_expire(server, db, key_sds, mstime() + seconds * 1000) != 0) {
        sds_delete(key_sds);
        add_reply_error(c, "ERR could not set expire");
        return;
    }
    sds_delete(key_sds);
    add_reply_long_long_with_prefix(c, 1, ':');
}

void ping_command(redis_client_t* c) {
    if (c->argc > 2) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }
    if (c->argc == 1) {
        add_reply(c, shared.pong);
    } else {
        add_reply_bulk(c, c->argv[1]);
    }
    
}

void save_command(redis_client_t* c) {
    LATTE_LIB_LOG(LOG_INFO, "save_command: start");
    redis_server_t* server = (redis_server_t*)c->client.server;
    const char* filename = "dump.ldb";
    
    if (c->argc > 2) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }
    if (c->argc == 2) {
        if (!sds_encoded_object(c->argv[1])) {
            add_reply_error(c, "ERR Invalid filename");
            return;
        }
        filename = (const char*)c->argv[1]->ptr;
    }
    
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        add_reply_error_format(c, "ERR Failed to open file: %s", filename);
        return;
    }
    
    oio* o = odb_oio_create_file(fp);
    if (!o) {
        fclose(fp);
        add_reply_error(c, "ERR Failed to create oio");
        return;
    }
    
    /* Save object manager registry first */
    LATTE_LIB_LOG(LOG_INFO, "save_command: saving registry");
    latte_error_t* err = object_manager_save_registry(o);
    if (err) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to save registry");
        error_delete(err);
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to save object registry");
        return;
    }
    LATTE_LIB_LOG(LOG_INFO, "save_command: registry saved");
    
    long long total_keys = 0;
    
    LATTE_LIB_LOG(LOG_INFO, "save_command: iterating databases, db_num=%d", server->db_num);
    if (!server->dbs) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: server->dbs is NULL");
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Internal error: server->dbs is NULL");
        return;
    }
    LATTE_LIB_LOG(LOG_INFO, "save_command: server->dbs is valid, starting loop");
    /* Iterate through all databases */
    for (int dbid = 0; dbid < server->db_num; dbid++) {
        LATTE_LIB_LOG(LOG_INFO, "save_command: checking db %d", dbid);
        redis_db_t* db = server->dbs + dbid;
        if (!db) {
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d is NULL", dbid);
            continue;
        }
        LATTE_LIB_LOG(LOG_INFO, "save_command: db %d is valid, checking keys", dbid);
        if (!db->keys) {
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d keys is NULL", dbid);
            continue;
        }
        LATTE_LIB_LOG(LOG_INFO, "save_command: db %d has keys, num_dicts=%lld", dbid, (long long)db->keys->num_dicts);
        /* Check if database has any keys */
        int has_keys = 0;
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d checking dict %lld", dbid, didx);
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) {
                LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld is NULL", dbid, didx);
                continue;
            }
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld is valid, getting size", dbid, didx);
            long long dict_sz = dict_size(d);
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld size=%lld", dbid, didx, dict_sz);
            if (dict_sz > 0) {
                LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld has %lld keys", dbid, didx, dict_sz);
                has_keys = 1;
                break;
            }
        }
        
        /* Only write SELECTDB if database has keys */
        if (!has_keys) {
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d has no keys, skipping", dbid);
            continue;
        }
        
        LATTE_LIB_LOG(LOG_INFO, "save_command: saving db %d", dbid);
        /* Write SELECTDB marker: 0xFF for SELECTDB, then dbid */
        LATTE_LIB_LOG(LOG_INFO, "save_command: writing SELECTDB for db %d", dbid);
        if (odb_write_u8(o, 0xFF) == 0 || odb_write_u32(o, dbid) == 0) {
            LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write SELECTDB for db %d", dbid);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to save database id");
            return;
        }
        
        LATTE_LIB_LOG(LOG_INFO, "save_command: iterating dicts in db %d", dbid);
        /* Iterate through all dicts in kv_store */
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d getting dict %lld", dbid, didx);
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) {
                LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld is NULL, skipping", dbid, didx);
                continue;
            }
            
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld is valid, creating iterator", dbid, didx);
            /* Iterate through all keys in this dict */
            latte_iterator_t* it = dict_get_latte_iterator(d);
            if (!it) {
                LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to create iterator for db %d dict %lld", dbid, didx);
                continue;
            }
            LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld iterator created, iterating keys", dbid, didx);
            int key_count = 0;
            while (protected_dict_iterator_has_next(it)) {
                LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld has next key %d", dbid, didx, key_count);
                latte_iterator_pair_t* pair = (latte_iterator_pair_t*)protected_dict_iterator_next(it);
                if (!pair) {
                    LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld pair is NULL, breaking", dbid, didx);
                    break;
                }
                LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld got pair, getting key and value", dbid, didx);
                sds key = (sds)iterator_pair_key(pair);
                latte_object_t* val = (latte_object_t*)iterator_pair_value(pair);
                
                LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld key=%p val=%p", dbid, didx, key, val);
                if (!key || !val) {
                    LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld key or val is NULL, skipping", dbid, didx);
                    continue;
                }
                LATTE_LIB_LOG(LOG_INFO, "save_command: db %d dict %lld processing key %d", dbid, didx, key_count++);
                
                /* Get expiration time */
                long long expire = db_get_expire(db, key);
                
                /* Write expiration time if exists: 0xFE for EXPIRETIME_MS */
                if (expire > 0) {
                    if (odb_write_u8(o, 0xFE) == 0 || odb_write_u64(o, (uint64_t)expire) == 0) {
                        protected_dict_iterator_delete(it);
                        odb_oio_free(o);
                        fclose(fp);
                        add_reply_error(c, "ERR Failed to save expiration time");
                        return;
                    }
                }
                
                /* Write key */
                if (odb_write_string(o, key, sds_len(key)) == 0) {
                    protected_dict_iterator_delete(it);
                    odb_oio_free(o);
                    fclose(fp);
                    add_reply_error(c, "ERR Failed to save key");
                    return;
                }
                
                /* Write value using object_manager */
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: saving object type=%u", (unsigned)val->type);
                err = object_manager_save(o, val);
                if (err) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to save object");
                    error_delete(err);
                    protected_dict_iterator_delete(it);
                    odb_oio_free(o);
                    fclose(fp);
                    add_reply_error(c, "ERR Failed to save object");
                    return;
                }
                
                total_keys++;
            }
            protected_dict_iterator_delete(it);
        }
    }
    
    /* Write EOF marker: 0xFD */
    if (odb_write_u8(o, 0xFD) == 0) {
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to write EOF");
        return;
    }
    
    /* Flush and close */
    if (o->flush(o) == 0) {
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to flush file");
        return;
    }
    
    odb_oio_free(o);
    fclose(fp);
    
    add_reply(c, shared.ok);
}

void load_command(redis_client_t* c) {
    redis_server_t* server = (redis_server_t*)c->client.server;
    const char* filename = "dump.ldb";
    
    if (c->argc > 2) {
        add_reply_error_format(c, "wrong number of arguments for '%s' command",
            c->cmd->name);
        return;
    }
    if (c->argc == 2) {
        if (!sds_encoded_object(c->argv[1])) {
            add_reply_error(c, "ERR Invalid filename");
            return;
        }
        filename = (const char*)c->argv[1]->ptr;
    }
    
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        add_reply_error_format(c, "ERR Failed to open file: %s", filename);
        return;
    }
    
    oio* o = odb_oio_create_file(fp);
    if (!o) {
        fclose(fp);
        add_reply_error(c, "ERR Failed to create oio");
        return;
    }
    
    /* Load object manager registry */
    uint8_t id_map[256];
    latte_error_t* err = object_manager_load_registry(o, id_map);
    if (err) {
        error_delete(err);
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to load object registry");
        return;
    }
    
    /* Clear all existing data */
    for (int dbid = 0; dbid < server->db_num; dbid++) {
        redis_db_t* db = server->dbs + dbid;
        if (!db || !db->keys) continue;
        
        /* Iterate through all dicts and delete all keys */
        /* Use a two-pass approach: first collect all keys, then delete them */
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) continue;
            
            /* First pass: collect all keys */
            sds* keys_to_delete = NULL;
            size_t key_count = 0;
            size_t key_capacity = 0;
            
            latte_iterator_t* it = dict_get_latte_iterator(d);
            if (!it) continue;
            
            while (protected_dict_iterator_has_next(it)) {
                latte_iterator_pair_t* pair = (latte_iterator_pair_t*)protected_dict_iterator_next(it);
                if (!pair) break;
                sds key = (sds)iterator_pair_key(pair);
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
                            protected_dict_iterator_delete(it);
                            break;
                        }
                        keys_to_delete = new_keys;
                        key_capacity = new_capacity;
                    }
                    keys_to_delete[key_count++] = sds_dup(key);
                }
            }
            protected_dict_iterator_delete(it);
            
            /* Second pass: delete all collected keys */
            for (size_t i = 0; i < key_count; i++) {
                /* db_delete_key will find and delete by content, freeing the dict's key */
                db_delete_key(server, db, keys_to_delete[i]);
                /* Free our copy */
                sds_delete(keys_to_delete[i]);
            }
            if (keys_to_delete) zfree(keys_to_delete);
        }
    }
    
    int current_db = 0;
    long long total_keys = 0;
    
    /* Load data */
    while (1) {
        uint8_t opcode;
        if (odb_read_u8(o, &opcode) == 0) {
            /* EOF or read error */
            break;
        }
        
        /* Check for EOF marker: 0xFD */
        if (opcode == 0xFD) {
            break;
        }
        
        /* SELECTDB marker: 0xFF */
        if (opcode == 0xFF) {
            uint32_t dbid;
            if (odb_read_u32(o, &dbid) == 0) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read database id");
                return;
            }
            if (dbid >= server->db_num) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error_format(c, "ERR Invalid database id: %u", dbid);
                return;
            }
            current_db = (int)dbid;
            continue;
        }
        
        long long expire = 0;
        /* EXPIRETIME_MS marker: 0xFE */
        if (opcode == 0xFE) {
            uint64_t expire64;
            if (odb_read_u64(o, &expire64) == 0) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read expiration time");
                return;
            }
            expire = (long long)expire64;
            /* Read next opcode */
            if (odb_read_u8(o, &opcode) == 0) {
                odb_oio_free(o);
                fclose(fp);
                add_reply_error(c, "ERR Failed to read opcode after expiration time");
                return;
            }
        }
        
        /* Read key */
        sds key = odb_read_string(o);
        if (!key) {
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to read key");
            return;
        }
        
        /* Read value using object_manager */
        void* obj_ptr = NULL;
        err = object_manager_load(o, &obj_ptr, id_map);
        if (err) {
            error_delete(err);
            sds_delete(key);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to load object");
            return;
        }
        
        latte_object_t* val = (latte_object_t*)obj_ptr;
        if (!val) {
            sds_delete(key);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to load object");
            return;
        }
        
        /* Add key-value to database */
        redis_db_t* db = server->dbs + current_db;
        if (!db) {
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Invalid database");
            return;
        }
        
        latte_object_t* key_obj = latte_object_string_new( key);
        if (!key_obj) {
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to create key object");
            return;
        }
        
        int ret = db_add_key_value(server, db, key_obj, val);
        latte_object_decr_ref_count(key_obj);
        
        if (ret != 0) {
            sds_delete(key);
            object_manager_release_object(val);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to add key-value to database");
            return;
        }
        
        /* Set expiration if exists */
        /* Find the actual key stored in database */
        if (expire > 0) {
            int didx = get_kv_store_index_for_key(key);
            dict_entry_t* de = kv_store_dict_find(db->keys, didx, key);
            if (de) {
                sds stored_key = (sds)dict_get_entry_key(de);
                if (stored_key) {
                    /* db_set_expire will copy the key, so we can use stored_key */
                    if (db_set_expire(server, db, stored_key, expire) != 0) {
                        /* Failed to set expire, but continue loading */
                        /* Don't fail the entire load operation for expire errors */
                    }
                }
            }
        }
        
        sds_delete(key);  /* Free the temporary key */
        
        total_keys++;
    }
    
    odb_oio_free(o);
    fclose(fp);
    
    add_reply(c, shared.ok);
}

/*  ==================== commands ==================== */

struct redis_command_t redis_command_table[] = {
    {
        "quit", quit_command, 0,
        "admin",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "module", module_command, -2,
        "admin no-script",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "ping", ping_command, -2,
        "admin",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "info", info_command, -2,
        "admin",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "expire", expire_command, 3,
        "write fast",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "save", save_command, -1,
        "admin",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    },
    {
        "load", load_command, -1,
        "admin",
        0, NULL, NULL, SWAP_NOP, 0, 0, 0, 0, 0, 0, 0
    }
};

int command_manager_register_command(command_manager_t* cm, redis_command_t* cmd) {
    cmd->id = acl_get_command_id(cm, cmd->name);
    return dict_add(cm->commands, sds_new(cmd->name), cmd);
}

void command_manager_init(command_manager_t* cm) {
    int j;
    int num_commands = sizeof(redis_command_table)/sizeof(struct redis_command_t);

    for (j = 0; j < num_commands; j++) {
        struct redis_command_t *c = redis_command_table + j;
        int retval1, retval2;
        //string -> int
        if (populate_command_table_parse_flags(c,  c->sflags) == -1) {
            latte_panic("unsupported command flag");
        }

        retval1 = command_manager_register_command(cm, c);
        LATTE_LIB_LOG(LOG_DEBUG, "register %s command", c->name);
    }
}



dict_func_t command_table_dict_type = {
    dict_sds_case_hash,
    NULL,
    NULL,
    dict_sds_key_case_compare,
    dict_sds_destructor,
    NULL,
    NULL
};

command_manager_t* command_manager_new() {
    command_manager_t* cm = (command_manager_t*)zmalloc(sizeof(command_manager_t));
    cm->commands = dict_new(&command_table_dict_type);
    cm->commandId = raxNew();
    cm->nextid = 0;
    command_manager_init(cm);
    return cm;
}

void command_manager_delete(command_manager_t* cm) {
    dict_delete(cm->commands);
    raxFree(cm->commandId);
    zfree(cm);
}



