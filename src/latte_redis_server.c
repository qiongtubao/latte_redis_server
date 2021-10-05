#include "latte_redis_server.h"
#include "util/util.h"
#include <locale.h>
#include <time.h>
#include <sys/time.h>
#include "log/log.h"
#include <stdlib.h>
#include <stdio.h>
#include <crc64/crc64.h>
#include <sys/stat.h>
#include "util/util.h"


// #include "client.h"
struct redisServer server;
void getCommand(client *c) {
    UNUSED(c);
}
/**
 * 
 * 
 * 
 * 
 */
struct redisCommand redisCommandTable[] = {
    // {
    //     "module", moduleCommand, -2, 
    //     "admin no-script"
    // }
    {"get",getCommand,2,
     "read-only fast @string",
     {{"read",
       KSPEC_BS_INDEX,.bs.index={1},
       KSPEC_FK_RANGE,.fk.range={0,1,0}}}}
};

/**
 *  设置内存溢出值 
 */
void redisOutOfMemoryHandler(size_t allocation_size) {
    serverLog(LL_WARNING,"Out Of Memory allocating %zu bytes!",
        allocation_size);
    panic("Redis aborting for OUT OF MEMORY. Allocating %zu bytes!",
        allocation_size);
}


/* Returns 1 if there is --sentinel among the arguments or if
 * executable name contains "redis-sentinel". 
     返回1的话  执行redis-sentinel
 */
int checkForSentinelMode(int argc, char **argv, char *exec_name) {
    if (strstr(exec_name,"redis-sentinel") != NULL) return 1;

    for (int j = 1; j < argc; j++)
        if (!strcmp(argv[j],"--sentinel")) return 1;
    return 0;
}



/* We take a cached value of the unix time in the global state because with
 * virtual memory and aging there is to store the current time in objects at
 * every object access, and accuracy is not needed. To access a global var is
 * a lot faster than calling time(NULL).
 *
 * This function should be fast because it is called at every command execution
 * in call(), so it is possible to decide if to update the daylight saving
 * info or not using the 'update_daylight_info' argument. Normally we update
 * such info only when calling this function from serverCron() but not when
 * calling it from call(). */
void updateCachedTime(int update_daylight_info) {
    server.ustime = ustime();
    server.mstime = server.ustime / 1000;
    time_t unixtime = server.mstime / 1000;
    atomicSet(server.unixtime, unixtime);

    /* To get information about daylight saving time, we need to call
     * localtime_r and cache the result. However calling localtime_r in this
     * context is safe since we will never fork() while here, in the main
     * thread. The logging function will call a thread safe version of
     * localtime that has no locks. */
    if (update_daylight_info) {
        struct tm tm;
        time_t ut = server.unixtime;
        localtime_r(&ut,&tm);
        server.daylight_active = tm.tm_isdst;
    }
}

/**
 * 初始化配置文件
 */ 
void initServerConfig(void) {
    int j;
    char *default_bindaddr[CONFIG_DEFAULT_BINDADDR_COUNT] = CONFIG_DEFAULT_BINDADDR;

    updateCachedTime(1);
    getRandomHexChars(server.runid,CONFIG_RUN_ID_SIZE);
    server.runid[CONFIG_RUN_ID_SIZE] = '\0';
    changeReplicationId();
    clearReplicationId2();
    server.hz = CONFIG_DEFAULT_HZ; /* Initialize it ASAP, even if it may get
                                      updated later after loading the config.
                                      This value may be used before the server
                                      is initialized. */
    server.timezone = getTimeZone(); /* Initialized by tzset(). */
    // server.configfile = NULL;
    // server.executable = NULL;
    // server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
    // server.bindaddr_count = CONFIG_DEFAULT_BINDADDR_COUNT;
    // for (j = 0; j < CONFIG_DEFAULT_BINDADDR_COUNT; j++)
    //     server.bindaddr[j] = zstrdup(default_bindaddr[j]);
    // server.bind_source_addr = NULL;
    // server.unixsocketperm = CONFIG_DEFAULT_UNIX_SOCKET_PERM;
    // server.ipfd.count = 0;
    // server.tlsfd.count = 0;
    // server.sofd = -1;
    // server.active_expire_enabled = 1;
    // server.skip_checksum_validation = 0;
    // server.saveparams = NULL;
    // server.loading = 0;
    // server.loading_rdb_used_mem = 0;
    // server.logfile = zstrdup(CONFIG_DEFAULT_LOGFILE);
    // server.aof_state = AOF_OFF;
    // server.aof_rewrite_base_size = 0;
    // server.aof_rewrite_scheduled = 0;
    // server.aof_flush_sleep = 0;
    // server.aof_last_fsync = time(NULL);
    // atomicSet(server.aof_bio_fsync_status,C_OK);
    // server.aof_rewrite_time_last = -1;
    // server.aof_rewrite_time_start = -1;
    // server.aof_lastbgrewrite_status = C_OK;
    // server.aof_delayed_fsync = 0;
    // server.aof_fd = -1;
    // server.aof_selected_db = -1; /* Make sure the first time will not match */
    // server.aof_flush_postponed_start = 0;
    // server.pidfile = NULL;
    // server.active_defrag_running = 0;
    // server.notify_keyspace_events = 0;
    // server.blocked_clients = 0;
    // memset(server.blocked_clients_by_type,0,
    //        sizeof(server.blocked_clients_by_type));
    // server.shutdown_asap = 0;
    // server.cluster_configfile = zstrdup(CONFIG_DEFAULT_CLUSTER_CONFIG_FILE);
    // server.cluster_module_flags = CLUSTER_MODULE_FLAG_NONE;
    // server.migrate_cached_sockets = dictCreate(&migrateCacheDictType);
    // server.next_client_id = 1; /* Client IDs, start from 1 .*/
    // server.loading_process_events_interval_bytes = (1024*1024*2);
    // server.page_size = sysconf(_SC_PAGESIZE);
    // server.pause_cron = 0;

    // unsigned int lruclock = getLRUClock();
    // atomicSet(server.lruclock,lruclock);
    // resetServerSaveParams();

    // appendServerSaveParams(60*60,1);  /* save after 1 hour and 1 change */
    // appendServerSaveParams(300,100);  /* save after 5 minutes and 100 changes */
    // appendServerSaveParams(60,10000); /* save after 1 minute and 10000 changes */

    // /* Replication related */
    // server.masterauth = NULL;
    // server.masterhost = NULL;
    // server.masterport = 6379;
    // server.master = NULL;
    // server.cached_master = NULL;
    // server.master_initial_offset = -1;
    // server.repl_state = REPL_STATE_NONE;
    // server.repl_transfer_tmpfile = NULL;
    // server.repl_transfer_fd = -1;
    // server.repl_transfer_s = NULL;
    // server.repl_syncio_timeout = CONFIG_REPL_SYNCIO_TIMEOUT;
    // server.repl_down_since = 0; /* Never connected, repl is down since EVER. */
    // server.master_repl_offset = 0;

    // /* Replication partial resync backlog */
    // server.repl_backlog = NULL;
    // server.repl_backlog_histlen = 0;
    // server.repl_backlog_idx = 0;
    // server.repl_backlog_off = 0;
    // server.repl_no_slaves_since = time(NULL);

    // /* Failover related */
    // server.failover_end_time = 0;
    // server.force_failover = 0;
    // server.target_replica_host = NULL;
    // server.target_replica_port = 0;
    // server.failover_state = NO_FAILOVER;

    // /* Client output buffer limits */
    // for (j = 0; j < CLIENT_TYPE_OBUF_COUNT; j++)
    //     server.client_obuf_limits[j] = clientBufferLimitsDefaults[j];

    // /* Linux OOM Score config */
    // for (j = 0; j < CONFIG_OOM_COUNT; j++)
    //     server.oom_score_adj_values[j] = configOOMScoreAdjValuesDefaults[j];

    // /* Double constants initialization */
    // R_Zero = 0.0;
    // R_PosInf = 1.0/R_Zero;
    // R_NegInf = -1.0/R_Zero;
    // R_Nan = R_Zero/R_Zero;

    // /* Command table -- we initialize it here as it is part of the
    //  * initial configuration, since command names may be changed via
    //  * redis.conf using the rename-command directive. */
    // server.commands = dictCreate(&commandTableDictType);
    // server.orig_commands = dictCreate(&commandTableDictType);
    // populateCommandTable();

    // /* Debugging */
    // server.watchdog_period = 0;

    // /* By default we want scripts to be always replicated by effects
    //  * (single commands executed by the script), and not by sending the
    //  * script to the slave / AOF. This is the new way starting from
    //  * Redis 5. However it is possible to revert it via redis.conf. */
    // server.lua_always_replicate_commands = 1;

    // initConfigValues();
}



int main(int argc, char **argv) {
    struct timeval tv;
    int j;
    char config_from_stdin = 0;
      /* We need to initialize our libraries, and the server configuration. */
#ifdef INIT_SETPROCTITLE_REPLACEMENT
    spt_init(argc, argv);
#endif
    setlocale(LC_COLLATE,"");
    tzset();
    zmalloc_set_oom_handler(redisOutOfMemoryHandler);
    srand(time(NULL)^getpid());
    srandom(time(NULL)^getpid());
    gettimeofday(&tv,NULL);
    init_genrand64(((long long) tv.tv_sec * 1000000 + tv.tv_usec) ^ getpid());
    crc64_init();
    /* Store umask value. Because umask(2) only offers a set-and-get API we have
     * to reset it and restore it back. We do this early to avoid a potential
     * race condition with threads that could be creating files or directories.
     */
    umask(server.umask = umask(0777));

    uint8_t hashseed[16];
    getRandomBytes(hashseed,sizeof(hashseed));
    dictSetHashFunctionSeed(hashseed);

    char *exec_name = strrchr(argv[0], '/');
    if (exec_name == NULL) exec_name = argv[0];
    server.sentinel_mode = checkForSentinelMode(argc,argv, exec_name);
    initServerConfig();
    return 1;
}