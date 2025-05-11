#include "crons.h"
#include <stdio.h>
#include <zmalloc/zmalloc.h>
crons_t* crons_new() {
    crons_t* c = zmalloc(sizeof(crons_t));
    c->cronloops = 0;
    c->crons = list_new();
    return c;
}



int addCron(crons_t* cs, cron_t* c) {
    
}

int removeCron(crons_t* cs, cron_t* c) {

}

cron_t* cron_new(long long period, exec_cron_fn* fn) {
    cron_t* c = zmalloc(sizeof(cron_t));
    c->period = period;
    c->fn = fn;
    return c;
}

int cronloop(crons_t* cs, void* p) {
    // listIter* iter = listGetIterator(cs->crons);
    // while ((node = listNext(iter)) != NULL) {
    //    cron* c = listNodeValue(node);
    //    c.fn(p);
    // }
    // listReleaseIterator(iter);
    // c.cronloops++;
    return 1;
}