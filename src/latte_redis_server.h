#include "redis_command.h"
#include <dict/dict.h>
#include "cluster.h"
#include "atomicvar.h"

//config
#define CONFIG_DEFAULT_UNIX_SOCKET_PERM 0
#define CONFIG_DEFAULT_BINDADDR_COUNT 2
#define CONFIG_DEFAULT_BINDADDR { "*", "-::*" }
#define CONFIG_DEFAULT_LOGFILE ""
#define CONFIG_DEFAULT_CLUSTER_CONFIG_FILE "nodes.conf"

/* Static server configuration */
#define CONFIG_DEFAULT_HZ        10             /* Time interrupt calls/sec. */
#define CONFIG_MIN_HZ            1
#define CONFIG_MAX_HZ            500



typedef struct redisServer {
    //基础
    // pid_t pid;
    // pthread_t main_thread_id;
    char *configfile;
    char *executable;
    char **exec_argv;
    int dynamic_hz;
    int config_hz;

    char runid[CONFIG_RUN_ID_SIZE+1];  /* ID always different at every exec. */
    /* Replication (master) */
    char replid[CONFIG_RUN_ID_SIZE+1];  /* My current replication ID. */
    char replid2[CONFIG_RUN_ID_SIZE+1]; /* replid inherited from master*/
    long long master_repl_offset;   /* My current replication offset */
    long long second_replid_offset; /* Accept offsets up to this for replid2. */
    
    /* time cache */
    redisAtomic time_t unixtime; /* Unix time sampled every cron cycle. */
    time_t timezone;            /* Cached timezone. As set by tzset(). */
    int daylight_active;        /* Currently in daylight saving time. */
    mstime_t mstime;            /* 'unixtime' in milliseconds. */
    ustime_t ustime;            /* 'unixtime' in microseconds. */
    
    mode_t umask;               /* The umask value of the process on startup */
    int hz;                     /* serverCron() calls frequency in hertz */
    int sentinel_mode;          /* True if this instance is a Sentinel. */
} redisServer;
extern struct redisServer server;

//client callback
void addReplyError(client *c, const char *err);
void addReplyLoadedModules(client *c);
void addReplySubcommandSyntaxError(client *c);
void moduleCommand(client *c);
//replication
void changeReplicationId(void);
void clearReplicationId2(void);