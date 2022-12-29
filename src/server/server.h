
#ifndef __LATTE_SERVER_H
#define __LATTE_SERVER_H
#include "crons.h"
typedef struct latteServer {
    /* General */
    pid_t pid;  
    pthread_t main_thread_id;
    aeEventLoop *el;
    long long port; /* server port */
    crons *crons;
} latteServer;

latteServer* createServer(aeEventLoop* el);
int startServer(latteServer* server);
int stopServer(latteServer* server);

#endif