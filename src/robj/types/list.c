#include "../robj.h"
#include "quicklist/quicklist.h"
void freeListObject(robj *o) {
    if (o->encoding == OBJ_ENCODING_QUICKLIST) {
        quicklistRelease(o->ptr);
    } else {
        panic("Unknown list encoding type");
    }
}