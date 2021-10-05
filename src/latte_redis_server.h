#include "redis_command.h"
#include <dict/dict.h>






typedef struct redisServer {
    //基础
    // pid_t pid;
    // pthread_t main_thread_id;
    char *configfile;
    char *executable;
    char **exec_argv;
    int dynamic_hz;
    int config_hz;


    mode_t umask;               /* The umask value of the process on startup */
} redisServer;


void addReplyError(client *c, const char *err);
void addReplyLoadedModules(client *c);
void addReplySubcommandSyntaxError(client *c);
void moduleCommand(client *c);
