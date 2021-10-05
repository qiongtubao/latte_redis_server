#include "latte_redis_server.h"
#include "util/util.h"
#include <locale.h>
#include <time.h>
#include <sys/time.h>
#include "log/log.h"
#include <stdlib.h>
#include <stdio.h>
#include <crc64/crc64.h>
#include <sys/stat.h>
#include "util/util.h"

// #include "client.h"
struct redisServer server;
void getCommand(client *c) {
    UNUSED(c);
}
/**
 * 
 * 
 * 
 * 
 */
struct redisCommand redisCommandTable[] = {
    // {
    //     "module", moduleCommand, -2, 
    //     "admin no-script"
    // }
    {"get",getCommand,2,
     "read-only fast @string",
     {{"read",
       KSPEC_BS_INDEX,.bs.index={1},
       KSPEC_FK_RANGE,.fk.range={0,1,0}}}}
};

void redisOutOfMemoryHandler(size_t allocation_size) {
    serverLog(LL_WARNING,"Out Of Memory allocating %zu bytes!",
        allocation_size);
    panic("Redis aborting for OUT OF MEMORY. Allocating %zu bytes!",
        allocation_size);
}


int main(int argc, char **argv) {
    struct timeval tv;
    int j;
    char config_from_stdin = 0;
      /* We need to initialize our libraries, and the server configuration. */
#ifdef INIT_SETPROCTITLE_REPLACEMENT
    spt_init(argc, argv);
#endif
    setlocale(LC_COLLATE,"");
    tzset();
    zmalloc_set_oom_handler(redisOutOfMemoryHandler);
    srand(time(NULL)^getpid());
    srandom(time(NULL)^getpid());
    gettimeofday(&tv,NULL);
    init_genrand64(((long long) tv.tv_sec * 1000000 + tv.tv_usec) ^ getpid());
    crc64_init();
    /* Store umask value. Because umask(2) only offers a set-and-get API we have
     * to reset it and restore it back. We do this early to avoid a potential
     * race condition with threads that could be creating files or directories.
     */
    umask(server.umask = umask(0777));

    uint8_t hashseed[16];
    getRandomBytes(hashseed,sizeof(hashseed));
    return 1;
}