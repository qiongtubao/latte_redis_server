
#ifndef __REDIS_COMMAND_H
#define __REDIS_COMMAND_H
#include "object/object.h"

/* Command call flags, see call() function */
#define CMD_CALL_NONE 0
#define CMD_CALL_SLOWLOG (1<<0)
#define CMD_CALL_STATS (1<<1)
#define CMD_CALL_PROPAGATE_AOF (1<<2)
#define CMD_CALL_PROPAGATE_REPL (1<<3)
#define CMD_CALL_PROPAGATE (CMD_CALL_PROPAGATE_AOF|CMD_CALL_PROPAGATE_REPL)
#define CMD_CALL_FULL (CMD_CALL_SLOWLOG | CMD_CALL_STATS | CMD_CALL_PROPAGATE)
#define CMD_CALL_NOWRAP (1<<4)  /* Don't wrap also propagate array into
                                           MULTI/EXEC: the caller will handle it.  */
 
/* Command flags. Please check the command table defined in the server.c file
 * for more information about the meaning of every flag. */
#define CMD_WRITE (1ULL<<0)            /* "write" flag */
#define CMD_READONLY (1ULL<<1)         /* "read-only" flag */
#define CMD_DENYOOM (1ULL<<2)          /* "use-memory" flag */
#define CMD_MODULE (1ULL<<3)           /* Command exported by module. */
#define CMD_ADMIN (1ULL<<4)            /* "admin" flag */
#define CMD_PUBSUB (1ULL<<5)           /* "pub-sub" flag */
#define CMD_NOSCRIPT (1ULL<<6)         /* "no-script" flag */
#define CMD_RANDOM (1ULL<<7)           /* "random" flag */
#define CMD_SORT_FOR_SCRIPT (1ULL<<8)  /* "to-sort" flag */
#define CMD_LOADING (1ULL<<9)          /* "ok-loading" flag */
#define CMD_STALE (1ULL<<10)           /* "ok-stale" flag */
#define CMD_SKIP_MONITOR (1ULL<<11)    /* "no-monitor" flag */
#define CMD_SKIP_SLOWLOG (1ULL<<12)    /* "no-slowlog" flag */
#define CMD_ASKING (1ULL<<13)          /* "cluster-asking" flag */
#define CMD_FAST (1ULL<<14)            /* "fast" flag */
#define CMD_NO_AUTH (1ULL<<15)         /* "no-auth" flag */
#define CMD_MAY_REPLICATE (1ULL<<16)   /* "may-replicate" flag */
/* Command flags used by the module system. */
#define CMD_MODULE_GETKEYS (1ULL<<17)  /* Use the modules getkeys interface. */
#define CMD_MODULE_NO_CLUSTER (1ULL<<18) /* Deny on Redis Cluster. */


typedef struct redis_client_t redis_client_t;
#define MAX_KEYS_BUFFER 256
typedef struct {
    int keys_buf[MAX_KEYS_BUFFER];
    int *keys;
    int numkeys;
    int size;
} get_keys_result_t;
typedef void redis_command_proc_func(struct redis_client_t *c);
typedef int redis_get_keys_proc_func(struct redis_command_t *cmd, latte_object_t **argv, int argc, get_keys_result_t *result);
typedef int (*redis_get_key_requests_proc_func)(int dbid, struct redis_command_t *cmd, latte_object_t **argv, int argc, struct get_key_requests_result *result);
typedef struct redis_command_t {
    char* name;
    redis_command_proc_func * proc;
    int arity;
    char * sflags;
    uint64_t flags;

    redis_get_keys_proc_func* get_keys_proc;
    redis_get_key_requests_proc_func* get_key_requests_proc;
    int intention;
    uint32_t intention_flags;
    int firstkey;
    int lastkey;
    int keystep;
    long long microseconds, calls, rejected_calls, failed_calls;
    int id;

} redis_command_t;
unsigned long acl_get_command_id(const char *cmdname);


typedef long long time_point_t;
typedef long long time_duration_us_t;

typedef struct redis_command_trace_t {
    time_point_t ae_trigger_read_time;        //epoll 触发读取事件时间
    time_point_t ae_get_event_time;           //epoll 获得事件事件
    time_point_t read_time;                   //读取命令时间
    time_duration_us_t decode_command_time;   //解密耗时
    time_point_t before_call_time;            //执行命令前
    long long after_call_time;                //执行命令后
    time_duration_us_t encode_command_time;   //加密耗时
    long long write_time;                     //发送命令时间
} redis_command_trace_t;

void reset_redis_command_trace_t(redis_command_trace_t* trace);
#endif