
#include "crons.h"
#include "utils/utils.h"

void update_cache_time(redis_server_t* server) {
    server->unixtime = ustime();
}


void update_cache_time_cron(void* server) {
    update_cache_time((redis_server_t*)server);
}

int init_redis_server_crons(redis_server_t* redis_server) {
    // init_server_crons((void*)redis_server);
    cron_t* updateCacheTimeCron = cron_new(update_cache_time_cron, 1);
    cron_manager_add_cron(redis_server->server.cron_manager, updateCacheTimeCron);
    return 0;
}