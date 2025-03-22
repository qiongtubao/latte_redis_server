#ifndef __LATTE_SERVER_CRONS_H
#define __LATTE_SERVER_CRONS_H
#include <list/list.h>
typedef void (execCronFn)(void* p);
typedef struct cron {
    long long period;
    execCronFn* fn;
} cron;

typedef struct crons {
    long long cronloops;
    list_t* crons;
} crons;
crons* createCrons();

cron* createCron(long long period, execCronFn* c);

#endif