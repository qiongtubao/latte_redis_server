#include "server.h"
/* Create an event handler for accepting new connections in TCP or TLS domain sockets.
 * This works atomically for all socket fds */
int createSocketAcceptHandler(aeEventLoop * el,socketFds *sfd, aeFileProc *accept_handler) {
    int j;
    for (j = 0; j < sfd->count; j++) {
        if (aeCreateFileEvent(el, sfd->fd[j], AE_READABLE, accept_handler,NULL) == AE_ERR) {
            /* Rollback */
            for (j = j-1; j >= 0; j--) aeDeleteFileEvent(el, sfd->fd[j], AE_READABLE);
            return 0;
        }
    }
    return 1;
}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    latteServer* server = (latteServer*)clientData;
    cronloop(server.crons);
    return 1000/server.hz;
}

int startServer(latteServer* server) {
    /* Create the timer callback, this is our way to process many background
     * operations incrementally, like clients timeout, eviction of unaccessed
     * expired keys and so forth. */
    if (aeCreateTimeEvent(server.el, 1, serverCron, server, NULL) == AE_ERR) {
        serverPanic("Can't create event loop timers.");
        exit(1);
    }
    /* Create an event handler for accepting new connections in TCP and Unix
     * domain sockets. */
    if (!createSocketAcceptHandler(server.el, &server.ipfd, acceptTcpHandler)) {
        serverPanic("Unrecoverable error creating TCP socket accept handler.");
    }
    if (!createSocketAcceptHandler(server.el, &server.tlsfd, acceptTLSHandler)) {
        serverPanic("Unrecoverable error creating TLS socket accept handler.");
    }
    return 1;
}

latteServer* createServer(struct aeEventLoop *el) {
    latteServer* server = zmalloc(sizeof(latteServer));
    server.el = el;
    server.crons = createCrons();
    server.port = -1;
    return server;
}