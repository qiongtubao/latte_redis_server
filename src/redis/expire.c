

#include "expire.h"
#include "dict/dict.h"

void active_expire_cycle(redis_server_t* rs, int type) {
    unsigned long effort = server.active_expire_effort-1,
    config_keys_per_loop = ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP +
                           ACTIVE_EXPIRE_CYCLE_KEYS_PER_LOOP/4*effort,
    config_cycle_fast_duration = ACTIVE_EXPIRE_CYCLE_FAST_DURATION +
                                 ACTIVE_EXPIRE_CYCLE_FAST_DURATION/4*effort,
    config_cycle_slow_time_perc = ACTIVE_EXPIRE_CYCLE_SLOW_TIME_PERC +
                                  2*effort,
    config_cycle_acceptable_stale = ACTIVE_EXPIRE_CYCLE_ACCEPTABLE_STALE-
                                    effort;
    
    static unsigned int current_db = 0;
    static int time_limit_exit = 0;
    static long long last_fast_cycle = 0;

    int j, iteration = 0;
    int dbs_per_call = CRON_DBS_PER_CALL;
    int dbs_performed = 0;
    long long start = ustime(), timelimit, elapsed;

    if (isPausedActionsWithUpdate(PAUSE_ACTION_EXPIRE)) return;

    if (type == ACTIVE_EXPIRE_CYCLE_FAST) {
        if (!time_limit_exit && 
            rs.stat_expired_stale_perc < config_cycle_acceptable_stale) 
            return;
            
        if (start <last_fast_cycle + (long long)config_cycle_fast_duration*2) 
            return;
        last_fast_cycle = start;
    }

    if (dbs_per_call > rs.dbnum || time_limit_exit) 
        dbs_per_call = rs.dbnum;
    
    time_limit = config_cycle_slow_time_perc*1000000/server.hz/100;
    time_limit_exit = 0;
    if (time_limit <= 0) time_limit = 1;

    if (type == ACTIVE_EXPIRE_CYCLE_FAST)
        time_limit = config_cycle_fast_duration;
    
    long total_sampled = 0;
    long total_expired = 0;

    // latte_assert(rs.also_propagate.numops == 0);

    for (j = 0; dbs_performed < dbs_per_call && time_limit_exit == 0 && j < server.dbnum; j++) {
        expire_scan_data data;
        data.ttl_sum = 0;
        data.ttl_samples = 0;
        redis_db_t* db = rs.db + (current_db%rs.dbnum);
        data.db = db;

        int db_done = 0;
        int update_avg_ttl_times = 0, repeat = 0;
        current_db++;
        active_expire_hash_field_cycle(type);

        if (dictSize(db->expires)) 
            dbs_performed++;
        
    }

}