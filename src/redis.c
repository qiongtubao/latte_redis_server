// #include "server.h"
#include <stdio.h>
#include "redis.h"
#include "redis_server/client.h"


struct latte_redis_server_t redisServer;
/** utils  **/
/* Given the filename, return the absolute path as an SDS string, or NULL
 * if it fails for some reason. Note that "filename" may be an absolute path
 * already, this will be detected and handled correctly.
 *
 * The function does not try to normalize everything, but only the obvious
 * case of one or more "../" appearing at the start of "filename"
 * relative path. */
sds getAbsolutePath(char *filename) {
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


void freeConfig(struct config* c) {
    zfree(c);
}


/** latte server module **/
// int startServer(struct latteServer* server) {
//     printf("start latte server %lld!!!!!\n", server->port);
//     return 1;
// }

int stopRedisServer(latte_redis_server_t* server) {
    printf("stop latte server !!!!!\n");
    return 1;
}

/** About latte RedisServer **/
int startRedisServer(latte_redis_server_t* redisServer, int argc, sds* argv) {
    log_init();
    log_add_stdout(LATTE_LIB, LOG_DEBUG);
    LATTE_LIB_LOG(LOG_INFO, "start redis server ");
    redisServer->exec_argc = argc;
    redisServer->exec_argv = argv;
    //argv[0] is exec file
    redisServer->executable = getAbsolutePath(argv[0]);

    redisServer->config = create_server_config();
    
    //argv[1] maybe is config file
    int attribute_index = 1;
    if (argv[1][0] != '-') {
        redisServer->configfile = getAbsolutePath(argv[1]);
        if (load_config_from_file(redisServer->config, redisServer->configfile) == 0) {
            goto fail;
        }
        attribute_index++;
    }

    //add config attribute property
    if (load_config_from_argv(redisServer->config, argv + attribute_index, argc - attribute_index) == 0) {
        goto fail;
    }
    init_latte_server(&redisServer->server);
    LATTE_LIB_LOG(LOG_INFO, "init redis server ");
    redisServer->server.maxclients = config_get_int64(redisServer->config, "max-clients");
    redisServer->server.el = aeCreateEventLoop(1024);
    redisServer->server.tcp_backlog = config_get_int64(redisServer->config, "tcp-backlog"); 
    redisServer->server.port = config_get_int64(redisServer->config, "port");
    redisServer->server.maxclients = config_get_int64(redisServer->config, "max-clients");
    redisServer->server.createClient = createRedisClient;
    redisServer->server.freeClient = freeRedisClient;
    redisServer->server.bind = config_get_array(redisServer->config, "bind");
    LATTE_LIB_LOG(LOG_INFO, "init redis server config");
    start_latte_server(&redisServer->server);
    


    return 1;
fail:
    printf("start latte redis fail");
    return 0;
}