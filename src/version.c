#include "version.h"


int version() {
    //sha=%s:%d
    //redisGitSHA1(),
    // atoi(redisGitDirty()) > 0,

    // malloc=%s
    //ZMALLOC_LIB,


    // build=%llx
    //(unsigned long long) redisBuildId()
    printf(
        "Latte server v=%s " 
        "bits=%d \n",
        LATTE_VERSION,
        sizeof(long) == 4 ? 32 : 64
    );
    return 1;
}