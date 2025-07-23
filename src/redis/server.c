// #include "server.h"
#include <stdio.h>
#include "server.h"
#include "client.h"
#include "dict/dict_plugins.h"
#include "debug/latte_debug.h"

#include "utils/utils.h"
/** utils  **/
/* Given the filename, return the absolute path as an SDS string, or NULL
 * if it fails for some reason. Note that "filename" may be an absolute path
 * already, this will be detected and handled correctly.
 *
 * The function does not try to normalize everything, but only the obvious
 * case of one or more "../" appearing at the start of "filename"
 * relative path. */
sds get_absolute_path(char *filename) {
    char cwd[1024];
    sds abspath;
    sds relpath = sds_new(filename);

    relpath = sds_trim(relpath," \r\n\t");
    if (relpath[0] == '/') return relpath; /* Path is already absolute. */

    /* If path is relative, join cwd and relative path. */
    if (getcwd(cwd,sizeof(cwd)) == NULL) {
        sds_delete(relpath);
        return NULL;
    }
    abspath = sds_new(cwd);
    if (sds_len(abspath) && abspath[sds_len(abspath)-1] != '/')
        abspath = sds_cat(abspath,"/");

    /* At this point we have the current path always ending with "/", and
     * the trimmed relative path. Try to normalize the obvious case of
     * trailing ../ elements at the start of the path.
     *
     * For every "../" we find in the filename, we remove it and also remove
     * the last element of the cwd, unless the current cwd is "/". */
    while (sds_len(relpath) >= 3 &&
           relpath[0] == '.' && relpath[1] == '.' && relpath[2] == '/')
    {
        sds_range(relpath,3,-1);
        if (sds_len(abspath) > 1) {
            char *p = abspath + sds_len(abspath)-2;
            int trimlen = 1;

            while(*p != '/') {
                p--;
                trimlen++;
            }
            sds_range(abspath,0,-(trimlen+1));
        }
    }

    /* Finally glue the two parts together. */
    abspath = sds_cat_sds(abspath,relpath);
    sds_delete(relpath);
    return abspath;
}


// void free_config(config_t* c) {
//     zfree(c);
// }


/** latte server module **/
// int startServer(struct latteServer* server) {
//     printf("start latte server %lld!!!!!\n", server->port);
//     return 1;
// }

int stopRedisServer(struct redis_server_t* server) {
    printf("stop latte server !!!!!\n");
    return 1;
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

dict_func_t modules_dict_type = {
    dict_sds_case_hash,
    NULL,
    NULL,
    dict_sds_key_case_compare,
    dict_sds_destructor,
    NULL,
    NULL
};

void init_redis_server(struct redis_server_t* rs) {
    rs->proto_max_bulk_len = 1024 * 1024;
    rs->clients_to_close = list_new();
    rs->commands = dict_new(&command_table_dict_type);
    rs->modules = dict_new(&modules_dict_type);
    module_register_core_api(rs);
} 
struct shared_objects_t shared;
void init_shared_objects() {
    int j;
    shared.crlf = latte_object_new(OBJ_STRING,sds_new("\r\n"));
    shared.ok = latte_object_new(OBJ_STRING,sds_new("+OK\r\n"));
    shared.pong = latte_object_new(OBJ_STRING,sds_new("+PONG\r\n"));
    shared.wrongtypeerr = latte_object_new(OBJ_STRING, sds_new("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"));
    for (j = 0; j < OBJ_SHARED_BULKHDR_LEN; j++) {
        shared.mbulkhdr[j] = latte_object_new(OBJ_STRING,
            sds_cat_printf(sds_empty(),"*%d\r\n",j));
        shared.bulkhdr[j] = latte_object_new(OBJ_STRING,
            sds_cat_printf(sds_empty(),"$%d\r\n",j));
    }
}



/** About latte RedisServer **/
int start_redis_server(struct redis_server_t* redis_server, int argc, sds* argv) {
    log_init();
    log_add_stdout(LATTE_LIB, LOG_DEBUG);
    LATTE_LIB_LOG(LOG_INFO, "start redis server %p", redis_server);
    init_redis_server(redis_server);
    init_shared_objects();
    register_commands(redis_server);
    redis_server->exec_argc = argc;
    redis_server->exec_argv = argv;
    //argv[0] is exec file
    redis_server->executable = get_absolute_path(argv[0]);

    redis_server->config = create_server_config();
    
    //argv[1] maybe is config file
    int attribute_index = 1;
    if (argv[1][0] != '-') {
        redis_server->configfile = get_absolute_path(argv[1]);
        if (load_config_from_file(redis_server->config, redis_server->configfile) == 0) {
            goto fail;
        }
        attribute_index++;
    }

    //add config attribute property
    if (load_config_from_argv(redis_server->config, argv + attribute_index, argc - attribute_index) == 0) {
        goto fail;
    }
    init_latte_server(&redis_server->server);
    LATTE_LIB_LOG(LOG_INFO, "init redis server ");
    redis_server->server.maxclients = config_get_int64(redis_server->config, "max-clients");
    redis_server->server.el = aeCreateEventLoop(1024);
    redis_server->server.tcp_backlog = config_get_int64(redis_server->config, "tcp-backlog"); 
    redis_server->server.port = config_get_int64(redis_server->config, "port");
    redis_server->server.maxclients = config_get_int64(redis_server->config, "max-clients");
    redis_server->server.createClient = create_redis_client;
    redis_server->server.freeClient = redis_client_delete;
    redis_server->server.bind = config_get_array(redis_server->config, "bind");
    redis_server->hz = 10;//config_get_int64(redis_server->config, "hz");
    redis_server->db_num = 16;
    LATTE_LIB_LOG(LOG_INFO, "init redis server config");
    init_redis_server_dbs(redis_server);
    update_cache_time(redis_server);
    init_redis_server_crons(redis_server);
    start_latte_server(&redis_server->server);
    return 1;
fail:
    printf("start latte redis fail");
    return 0;
}

void _redis_panic(const char *file, int line, const char *msg, ...) {
    va_list ap;
    va_start(ap,msg);
    char fmtmsg[256];
    vsnprintf(fmtmsg,sizeof(fmtmsg),msg,ap);
    va_end(ap);
    LATTE_LIB_LOG(LOG_ERROR, fmtmsg);
}



