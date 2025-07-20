#ifndef __LATTE_REDIS_SERVER_H
#define __LATTE_REDIS_SERVER_H
#include "dict/dict.h"
#include "ae/ae.h"
#include <pthread.h>
#include "sds/sds.h"

#include "redis/config.h"
#include "redis/server.h"

#define PRIVATE 



#include "config/config.h"
#include "server/server.h"
#include "list/list.h"
#define PRIVATE 
typedef struct latte_redis_server_t {
    latte_server_t server;
    int exec_argc;
    sds* exec_argv;
    sds executable; /** execut file path **/
    sds configfile;
    struct config* config;
    dict_t* commands;
    dict_t* robj_register;
    list_t* clients_pending_write;
    long long proto_max_bulk_len;   /* Protocol bulk length maximum size. */
    long long unixtime;
} latte_redis_server_t;


extern struct latte_redis_server_t redisServer;
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

// // PRIVATE int start_redis_server(struct redis_server_t* redisServer, int argc, sds* argv);




// PRIVATE int startRedisServer(struct latte_redis_server_t* redisServer, int argc, sds* argv);
// extern struct latte_redis_server_t redisServer;

#endif