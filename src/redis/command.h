
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


#endif