

#include "command_manager.h"
#include "zmalloc/zmalloc.h"
#include "dict/dict_plugins.h"
#include "../shared/shared.h"

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



