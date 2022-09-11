
#include "dict/dict.h"
#include "ae/ae.h"
#include <pthread.h>
struct latteServer {
    /* General */
    pid_t pid;  
    pthread_t main_thread_id;
    char *configfile;
    dict* commands;
    dict* robj_register;
    aeEventLoop *el;
} latteServer;

/* Global vars */
struct latteServer server; /* Server global state */
int startServer();
int stopServer();
