
#ifndef __REDIS_COMMAND_H
#define __REDIS_COMMAND_H
#include "object/object.h"
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
} redis_command_t;


#endif