#include "server.h"
#include <stdio.h>
/** utils  **/
/* Given the filename, return the absolute path as an SDS string, or NULL
 * if it fails for some reason. Note that "filename" may be an absolute path
 * already, this will be detected and handled correctly.
 *
 * The function does not try to normalize everything, but only the obvious
 * case of one or more "../" appearing at the start of "filename"
 * relative path. */
sds getAbsolutePath(char *filename) {
    char cwd[1024];
    sds abspath;
    sds relpath = sdsnew(filename);

    relpath = sdstrim(relpath," \r\n\t");
    if (relpath[0] == '/') return relpath; /* Path is already absolute. */

    /* If path is relative, join cwd and relative path. */
    if (getcwd(cwd,sizeof(cwd)) == NULL) {
        sdsfree(relpath);
        return NULL;
    }
    abspath = sdsnew(cwd);
    if (sdslen(abspath) && abspath[sdslen(abspath)-1] != '/')
        abspath = sdscat(abspath,"/");

    /* At this point we have the current path always ending with "/", and
     * the trimmed relative path. Try to normalize the obvious case of
     * trailing ../ elements at the start of the path.
     *
     * For every "../" we find in the filename, we remove it and also remove
     * the last element of the cwd, unless the current cwd is "/". */
    while (sdslen(relpath) >= 3 &&
           relpath[0] == '.' && relpath[1] == '.' && relpath[2] == '/')
    {
        sdsrange(relpath,3,-1);
        if (sdslen(abspath) > 1) {
            char *p = abspath + sdslen(abspath)-2;
            int trimlen = 1;

            while(*p != '/') {
                p--;
                trimlen++;
            }
            sdsrange(abspath,0,-(trimlen+1));
        }
    }

    /* Finally glue the two parts together. */
    abspath = sdscatsds(abspath,relpath);
    sdsfree(relpath);
    return abspath;
}

/** latte config module **/
struct config* createConfig() {
    struct config* c = zmalloc(sizeof(struct config));
    return c;
}

void freeConfig(struct config* c) {
    zfree(c);
}

int loadConfigFile(struct config* c, sds file) {
    return 1;
}

int loadConfigFromSds(struct config* c, sds content) {
    return 1;
}

int loadConfigFromArgv(struct config* c, int argc, sds* argv) {
    // printf config 
    sds format_conf_sds = sdsempty();
    int i = 0;
    while(i < argc) {
        if (argv[i][0] == '-' && argv[i][1] == '-') {
            /** Option name **/
            if (sdslen(format_conf_sds)) format_conf_sds = sdscat(format_conf_sds, "\n");
            format_conf_sds = sdscat(format_conf_sds, argv[i] + 2);
            format_conf_sds = sdscat(format_conf_sds, " ");
        } else {
            /* maybe value is some params */
            /* Option argument*/
            format_conf_sds = sdscatrepr(format_conf_sds, argv[i], sdslen(argv[i]));
            format_conf_sds = sdscat(format_conf_sds, " ");
        }
        i++;
    }
    return loadConfigFromSds(c, format_conf_sds);
}

/** latte server module **/
int startServer(struct latteServer* server) {
    printf("start latte server !!!!!\n");
    return 1;
}

int stopServer(struct latteServer* server) {
    printf("stop latte server !!!!!\n");
    return 1;
}

/** About latte RedisServer **/
int startRedisServer(struct latteRedisServer* redisServer, int argc, sds* argv) {
    redisServer->exec_argc = argc;
    redisServer->exec_argv = argv;
    //argv[0] is exec file
    redisServer->executable = getAbsolutePath(argv[0]);

    redisServer->config = createConfig();
    //argv[1] maybe is config file
    int attribute_index = 1;
    if (argv[1][0] != '-') {
        redisServer->configfile = getAbsolutePath(argv[1]);
        if (loadConfigFile(redisServer->config, redisServer->configfile) == 0) {
            goto fail;
        }
        attribute_index++;
    }
    //add config attribute property
    if (loadConfigFromArgv(redisServer->config, argc - attribute_index, argv + attribute_index) == 0) {
        goto fail;
    }

    startServer(&redisServer->server);
    return 1;
fail:
    printf("start latte redis fail");
    return 0;
}