#include "latte_redis_server.h"
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




int main() {
    return 1;
}