#include "shared.h"

struct shared_objects_t shared;
void init_shared_objects() {
    int j;
    shared.crlf = latte_object_string_new(sds_new("\r\n"));
    latte_assert(sds_encoded_object(shared.crlf) == 1);
    shared.ok = latte_object_string_new(sds_new("+OK\r\n"));
    shared.pong = latte_object_string_new(sds_new("+PONG\r\n"));
    shared.wrongtypeerr = latte_object_string_new(sds_new("-WRONGTYPE Operation against a key holding the wrong kind of value\r\n"));
    for (j = 0; j < OBJ_SHARED_BULKHDR_LEN; j++) {
        shared.mbulkhdr[j] = latte_object_string_new(
            sds_cat_printf(sds_empty(),"*%d\r\n",j));
        shared.bulkhdr[j] = latte_object_string_new(
            sds_cat_printf(sds_empty(),"$%d\r\n",j));
    }
}