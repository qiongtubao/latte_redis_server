#include "main.h"

#include "version.h"
#include "help.h"
#include <string.h>

struct latteRedisServer redisServer;


sds* parseArgv(int argc, char** argv, int* len) {
    char** result = zmalloc(sizeof(sds)*(argc + 1));
    //argv[0] is config path
    // result[0] = getAbsolutePath(argv[0]);
    result[argc] = NULL;
    for(int j = 0; j < argc; j++) {
        result[j] = sdsnewlen(argv[j], strlen(argv[j]));
    }
    *len = argc;
    return result;
}



typedef enum {
    HELP = 0,
    VERSION,
    TEST_MEMORY,
    CHECK_SYSTEM,
    SERVER,
} Mode;

Mode getMode(int argc, sds* argv) {
    if (argc == 1) return SERVER;
    if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) {
        return HELP;
    }
    if (strcmp(argv[1], "-v") == 0 || 
            strcmp(argv[1], "--version") == 0) {
        return VERSION;
    }
    if (strcmp(argv[1], "--test-memory") == 0) {
        return TEST_MEMORY;
    }
    if (strcmp(argv[1], "--check-system") == 0) {
        return CHECK_SYSTEM;
    }
    return SERVER;
}





int main(int argc, char **argv) {
    int exec_argc;
    sds* exec_argv = parseArgv(argc, argv, &exec_argc);
    Mode mode = getMode(exec_argc, exec_argv);
    switch (mode) {
        case HELP:
            help();
            goto end;
        case VERSION:
            version();
            goto end; 
        case SERVER:
            startRedisServer(&redisServer, exec_argc, exec_argv);
            goto end;
        default:
            break;
    }

end:
    for(int i = 0; i < exec_argc; i++) {
        sdsfree(exec_argv[i]);
    }
    zfree(exec_argv);
    return 1;
}