#include "latte_redis_server.h"
#include "util/util.h"
#include <locale.h>
#include <time.h>
#include <sys/time.h>
#include "log/log.h"
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
    return 1;
}