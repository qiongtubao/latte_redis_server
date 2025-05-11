#ifndef __LATTE_REDIS_SERVER_H
#define __LATTE_REDIS_SERVER_H
#include "dict/dict.h"
#include "ae/ae.h"
#include <pthread.h>
#include "sds/sds.h"
#include "redis/config.h"
#include "redis/server.h"

#define PRIVATE 



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
// PRIVATE int start_redis_server(struct redis_server_t* redisServer, int argc, sds* argv);

#endif