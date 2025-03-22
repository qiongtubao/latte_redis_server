#ifndef __LATTE_REDIS_SERVER_H
#define __LATTE_REDIS_SERVER_H
#include "dict/dict.h"
#include "ae/ae.h"
#include <pthread.h>
#include "sds/sds.h"
#include "config/config.h"
#include "server/server.h"
#define PRIVATE 
typedef struct latteRedisServer {
    struct latteServer server;
    int exec_argc;
    sds* exec_argv;
    sds executable; /** execut file path **/
    sds configfile;
    struct config* config;
    dict_t* commands;
    dict_t* robj_register;
} latteRedisServer;
/* Global vars */
config_manager_t* create_server_config();

/** latte redis server **/


/**
 * @brief 
 * 
 * @param redisServer 
 * @param argc 
 * @param argv 
 * @return int 
 *      启动redisServer 通过参数方式
 *      包含  init redisServer
 *      
 */


PRIVATE int startRedisServer(struct latteRedisServer* redisServer, int argc, sds* argv);

#endif