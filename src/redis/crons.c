
#include "crons.h"
#include "utils/utils.h"

void update_cache_time(struct latte_server_t* server) {
    redis_server_t* redis_server = (redis_server_t*)server;
    redis_server->unixtime = ustime();
}

int init_redis_server_crons(struct redis_server_t* redis_server) {
    init_server_crons(redis_server);
    cron_t* updateCacheTimeCron = cron_new(1, update_cache_time);
    crons_add(redis_server->server.crons, updateCacheTimeCron);
    return 0;
}