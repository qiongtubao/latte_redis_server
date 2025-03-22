#include "client.h"

int redisHandler(struct latteClient* client) {
    
    

    return 1;
}

latteClient* createRedisClient() {
    redis_client_t* c = zmalloc(sizeof(redis_client_t));
    c->client.exec = redisHandler;
    return c;
}
void freeRedisClient(latteClient* client) {
    zfree(client);
}