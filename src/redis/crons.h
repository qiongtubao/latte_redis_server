#ifndef __REDIS_CRONS_H
#define __REDIS_CRONS_H
#include <list/list.h>
typedef void (exec_cron_fn)(void* p);
typedef struct cron {
    long long period;
    exec_cron_fn* fn;
} cron_t;

typedef struct crons_t {
    long long cronloops;
    list_t* crons;
} crons_t;
crons_t* crons_new();

cron_t* cron_new(long long period, exec_cron_fn* c);

#endif