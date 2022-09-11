
#include "dict/dict.h"
#include "ae/ae.h"
#include <pthread.h>
#include "sds/sds.h"
struct latteServer {
    /* General */
    pid_t pid;  
    pthread_t main_thread_id;
    aeEventLoop *el;
} latteServer;

/* Global vars */
struct latteServer server; /* Server global state */
int startServer(struct latteServer* server);
int stopServer(struct latteServer* server);


/** config **/
struct configRule {

} configRule;
struct config {
    
} config;
struct config* createConfig();
int loadConfigFile(struct config* c, sds file);
void freeConfig(struct config* c);
int registerConfig(struct config* c, char* key, struct configRule* rule);
int getIntConfig(struct config* c, char* key);
sds getSdsConfig(struct config* c, char* key);
int setIntConfig(struct config* c, char* key, int value);
int setSdsConfig(struct config* c, char* key, sds value);

/** latte redis server **/
struct latteRedisServer {
    struct latteServer server;
    int exec_argc;
    sds* exec_argv;
    sds executable; /** execut file path **/
    sds configfile;
    struct config* config;
    dict* commands;
    dict* robj_register;
} latteRedisServer;

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
#define PRIVATE 

PRIVATE int startRedisServer(struct latteRedisServer* redisServer, int argc, sds* argv);

