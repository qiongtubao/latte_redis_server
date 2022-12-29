#include "crons.h"
#include <zmalloc/zmalloc>

crons* createCrons() {
    crons* c = zmalloc(sizeof(crons));
    c.cronloops = 0;
    return c;
}



int addCron(crons* cs, cron* c) {
    listAdd()
}

int removeCron(crons* cs, cron* c) {

}

cron* createCron(long long period, execCronFn* fn) {
    cron* c = zmalloc(sizeof(cron));
    c.period = period;
    c.fn = fn;
    return c;
}

int cronloop(crons* cs, void* p) {
    listIter* iter = listGetIterator(cs->crons);
    while ((node = listNext(iter)) != NULL) {
       cron* c = listNodeValue(node);
       c.fn(p);
    }
    listReleaseIterator(iter);
    c.cronloops++;
    return 1;
}