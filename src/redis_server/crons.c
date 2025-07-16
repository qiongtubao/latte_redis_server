#include "crons.h"
#include <stdio.h>
#include <zmalloc/zmalloc.h>
crons* createCrons() {
    crons* c = zmalloc(sizeof(crons));
    c->cronloops = 0;
    c->crons = list_new();
    return c;
}



int addCron(crons* cs, cron* c) {
    
}

int removeCron(crons* cs, cron* c) {

}

cron* createCron(long long period, execCronFn* fn) {
    cron* c = zmalloc(sizeof(cron));
    c->period = period;
    c->fn = fn;
    return c;
}

int cronloop(crons* cs, void* p) {
    // listIter* iter = listGetIterator(cs->crons);
    // while ((node = listNext(iter)) != NULL) {
    //    cron* c = listNodeValue(node);
    //    c.fn(p);
    // }
    // listReleaseIterator(iter);
    // c.cronloops++;
    return 1;
}