
#include "server/server.h"


typedef struct redis_client_t {
    latteClient client;
} redis_client_t;

latteClient* createRedisClient();
void freeRedisClient(latteClient* client);
