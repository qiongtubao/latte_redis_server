#include "../robj.h"
#include "stream/stream.h"

void freeStreamObject(robj *o) {
    freeStream(o->ptr);
}