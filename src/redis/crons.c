
#include "crons.h"
#include "utils/utils.h"
#include "time/localtime.h"

void update_cache_time(redis_server_t* server) {
    server->unixtime = ustime();
}


void update_cache_time_cron(void* server) {
    update_cache_time((redis_server_t*)server);
}

void update_metric_cron(void* server) {
    redis_server_t* rs = (redis_server_t*)server;
    long long current_time = ustime();
    long long factor = 1000000;  // us
    metric_track_instantaneous(rs->metric, "command", rs->metric_stat_numcommands, current_time, factor);
}

int init_redis_server_crons(redis_server_t* redis_server) {
    // init_server_crons((void*)redis_server);
    cron_t* updateCacheTimeCron = cron_new(update_cache_time_cron, 1);
    cron_manager_add_cron(redis_server->server.cron_manager, updateCacheTimeCron);
    cron_t* updateMetricCron = cron_new(update_metric_cron, 100);
    cron_manager_add_cron(redis_server->server.cron_manager, updateMetricCron);
    return 0;
}