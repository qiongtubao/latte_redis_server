#ifndef __REDIS_SHARED_H
#define __REDIS_SHARED_H
#include "sds/sds.h"
#include "object/string.h"
#define OBJ_SHARED_BULKHDR_LEN 32
typedef struct shared_objects_t {
   latte_object_t* crlf, *ok, *pong, *wrongtypeerr,
   *mbulkhdr[OBJ_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
   *bulkhdr[OBJ_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
} shared_objects_t;
extern struct shared_objects_t shared;

void init_shared_objects();

#endif