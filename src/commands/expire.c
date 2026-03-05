#include "command_manager.h"
#include "../redis/db.h"
#include "../redis/server.h"
#include "../object/string.h"
#include "../shared/shared.h"
#include "utils/utils.h"
#include <stdio.h>

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
