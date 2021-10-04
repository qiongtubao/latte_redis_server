#include "../robj.h"
#include "dict/dict.h"
#include "listpack/listpack.h"


void freeHashObject(robj *o) {
    switch (o->encoding) {
    case OBJ_ENCODING_HT:
        dictRelease((dict*) o->ptr);
        break;
    case OBJ_ENCODING_LISTPACK:
        lpFree(o->ptr);
        break;
    default:
        panic("Unknown hash encoding type");
        break;
    }
}