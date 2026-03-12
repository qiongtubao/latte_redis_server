
#include "crons.h"
#include "utils/utils.h"
#include "time/localtime.h"

/**
 * 更新服务器缓存时间，减少频繁的系统调用
 * 输入: server - Redis 服务器实例指针
 * 返回: 无
 */
void update_cache_time(redis_server_t* server) {
    server->unixtime = ustime();
}

/**
 * 定时任务包装函数，用于注册到定时任务管理器
 * 输入: server - Redis 服务器实例指针（void* 类型）
 * 返回: 无
 */
void update_cache_time_cron(void* server) {
    update_cache_time((redis_server_t*)server);
}

/**
 * 定时更新 QPS 等指标统计
 * factor=1000000 表示每秒统计一次
 * 输入: server - Redis 服务器实例指针（void* 类型）
 * 返回: 无
 */
void update_metric_cron(void* server) {
    redis_server_t* rs = (redis_server_t*)server;
    long long current_time = ustime();
    long long factor = 1000000;  // us，即每秒更新一次指标
    metric_track_instantaneous(rs->metric, "command", rs->metric_stat_numcommands, current_time, factor);
}

/**
 * 过期指标定时任务（暂未实现）
 * 输入: server - Redis 服务器实例指针（void* 类型）
 * 返回: 无
 */
void expire_metric_cron(void* server) {
    // redis_server_t* rs = (redis_server_t*)server;
    // (rs->expires);

}

/**
 * 注册所有 Redis 服务器定时任务
 * updateCacheTime: 每 1ms 执行一次，更新缓存时间
 * updateMetric: 每 100ms 执行一次，更新指标统计
 * 输入: redis_server - Redis 服务器实例指针
 * 返回: 0 表示成功
 */
int init_redis_server_crons(redis_server_t* redis_server) {
    // init_server_crons((void*)redis_server);
    cron_t* updateCacheTimeCron = cron_new(update_cache_time_cron, 1);
    cron_manager_add_cron(redis_server->server.cron_manager, updateCacheTimeCron);
    cron_t* updateMetricCron = cron_new(update_metric_cron, 100);
    cron_manager_add_cron(redis_server->server.cron_manager, updateMetricCron);
    // cron_t* activeExpireCycleCron = cron_new(expire_metric_cron, 1000);
    // cron_manager_add_cron(redis_server->server.cron_manager, activeExpireCycleCron);
    return 0;
}