#include "command_manager.h"
#include "../shared/shared.h"
#include "../redis/db.h"
#include "../redis/server.h"
#include "../object/string.h"
#include "../../deps/latte_c/src/odb/odb.h"
#include "../../deps/latte_c/src/object/object_manager.h"
#include "../../deps/latte_c/src/dict/dict.h"
#include "../../deps/latte_c/src/iterator/iterator.h"
#include "../../deps/latte_c/src/error/error.h"
#include <stdio.h>

void save_command(redis_client_t* c) {
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: start");
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
    
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: opening file %s", filename);
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to open file %s", filename);
        add_reply_error_format(c, "ERR Failed to open file: %s", filename);
        return;
    }
    
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: creating oio");
    oio* o = odb_oio_create_file(fp);
    if (!o) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to create oio");
        fclose(fp);
        add_reply_error(c, "ERR Failed to create oio");
        return;
    }
    
    /* Save object manager registry first */
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: saving registry");
    latte_error_t* err = object_manager_save_registry(o);
    if (err) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to save registry");
        error_delete(err);
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Failed to save object registry");
        return;
    }
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: registry saved");
    
    long long total_keys = 0;
    
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: iterating databases, db_num=%d", server->db_num);
    if (!server->dbs) {
        LATTE_LIB_LOG(LOG_ERROR, "save_command: server->dbs is NULL");
        odb_oio_free(o);
        fclose(fp);
        add_reply_error(c, "ERR Internal error: server->dbs is NULL");
        return;
    }
    LATTE_LIB_LOG(LOG_DEBUG, "save_command: server->dbs is valid, starting loop");
    /* Iterate through all databases */
    for (int dbid = 0; dbid < server->db_num; dbid++) {
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: checking db %d", dbid);
        redis_db_t* db = server->dbs + dbid;
        if (!db) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d is NULL", dbid);
            continue;
        }
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d is valid, checking keys", dbid);
        if (!db->keys) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d keys is NULL", dbid);
            continue;
        }
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d has keys, num_dicts=%lld", dbid, (long long)db->keys->num_dicts);
        /* Check if database has any keys */
        int has_keys = 0;
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d checking dict %lld", dbid, didx);
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) {
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is NULL", dbid, didx);
                continue;
            }
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is valid, getting size", dbid, didx);
            long long dict_sz = dict_size(d);
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld size=%lld", dbid, didx, dict_sz);
            if (dict_sz > 0) {
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld has %lld keys", dbid, didx, dict_sz);
                has_keys = 1;
                break;
            }
        }
        
        /* Only write SELECTDB if database has keys */
        if (!has_keys) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d has no keys, skipping", dbid);
            continue;
        }
        
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: saving db %d", dbid);
        /* Write SELECTDB marker: 0xFF for SELECTDB, then dbid */
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: writing SELECTDB for db %d", dbid);
        if (odb_write_u8(o, 0xFF) == 0 || odb_write_u32(o, dbid) == 0) {
            LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write SELECTDB for db %d", dbid);
            odb_oio_free(o);
            fclose(fp);
            add_reply_error(c, "ERR Failed to save database id");
            return;
        }
        
        LATTE_LIB_LOG(LOG_DEBUG, "save_command: iterating dicts in db %d", dbid);
        /* Iterate through all dicts in kv_store */
        for (long long didx = 0; didx < db->keys->num_dicts; didx++) {
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d getting dict %lld", dbid, didx);
            dict_t* d = kv_store_get_dict(db->keys, didx);
            if (!d) {
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is NULL, skipping", dbid, didx);
                continue;
            }
            
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld is valid, creating iterator", dbid, didx);
            /* Iterate through all keys in this dict */
            latte_iterator_t* it = dict_get_latte_iterator(d);
            if (!it) {
                LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to create iterator for db %d dict %lld", dbid, didx);
                continue;
            }
            LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld iterator created, iterating keys", dbid, didx);
            int key_count = 0;
            while (protected_dict_iterator_has_next(it)) {
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld has next key %d", dbid, didx, key_count);
                latte_iterator_pair_t* pair = (latte_iterator_pair_t*)protected_dict_iterator_next(it);
                if (!pair) {
                    LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld pair is NULL, breaking", dbid, didx);
                    break;
                }
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld got pair, getting key and value", dbid, didx);
                sds key = (sds)iterator_pair_key(pair);
                latte_object_t* val = (latte_object_t*)iterator_pair_value(pair);
                
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld key=%p (0x%lx) val=%p", dbid, didx, key, (unsigned long)key, val);
                if (!key || !val) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: db %d dict %lld key or val is NULL/invalid, skipping (key=%p (0x%lx), val=%p). This indicates a problem with dict entry structure!", dbid, didx, key, (unsigned long)key, val);
                    continue;
                }
                /* Validate key pointer - 0x3 is clearly invalid */
                if ((unsigned long)key < 0x1000) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: db %d dict %lld key pointer is suspiciously small: %p (0x%lx). This is likely a corrupted entry!", dbid, didx, key, (unsigned long)key);
                    continue;
                }
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld processing key %d, key_len=%zu", dbid, didx, key_count++, sds_len(key));
                
                /* Get expiration time */
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld getting expire for key", dbid, didx);
                long long expire = db_get_expire(db, key);
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld expire=%lld", dbid, didx, expire);
                
                /* Write expiration time if exists: 0xFE for EXPIRETIME_MS */
                if (expire > 0) {
                    LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld writing expire time", dbid, didx);
                    if (odb_write_u8(o, 0xFE) == 0 || odb_write_u64(o, (uint64_t)expire) == 0) {
                        LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write expire time");
                        protected_dict_iterator_delete(it);
                        odb_oio_free(o);
                        fclose(fp);
                        add_reply_error(c, "ERR Failed to save expiration time");
                        return;
                    }
                }
                
                /* Write key */
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld writing key, len=%zu", dbid, didx, sds_len(key));
                if (odb_write_string(o, key, sds_len(key)) == 0) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to write key");
                    protected_dict_iterator_delete(it);
                    odb_oio_free(o);
                    fclose(fp);
                    add_reply_error(c, "ERR Failed to save key");
                    return;
                }
                
                /* Write value using object_manager */
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld saving object type=%u, refcount=%d", 
                              dbid, didx, (unsigned)val->type, val->refcount);
                err = object_manager_save(o, val);
                if (err) {
                    LATTE_LIB_LOG(LOG_ERROR, "save_command: failed to save object, error code=%d", err->code);
                    error_delete(err);
                    protected_dict_iterator_delete(it);
                    odb_oio_free(o);
                    fclose(fp);
                    add_reply_error(c, "ERR Failed to save object");
                    return;
                }
                LATTE_LIB_LOG(LOG_DEBUG, "save_command: db %d dict %lld object saved successfully", dbid, didx);
                
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
