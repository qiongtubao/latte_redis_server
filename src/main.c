#include "main.h"

#include "version.h"
#include "help.h"
#include "server.h"
#include <string.h>


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

sds* parseArgv(int argc, char** argv, int* len) {
    char** result = zmalloc(sizeof(sds)*(argc + 1));
    //argv[0] is config path
    result[0] = getAbsolutePath(argv[0]);
    result[argc] = NULL;
    for(int j = 1; j < argc; j++) {
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
    int len;
    sds* exec_argv = parseArgv(argc, argv, &len);
    Mode mode = getMode(len, exec_argv);
    switch (mode) {
        case HELP:
            help();
            goto end;
        case VERSION:
            version();
            goto end;
        default:
            break;
    }

end:
    for(int i = 0; i < len; i++) {
        sdsfree(exec_argv[i]);
    }
    zfree(exec_argv);
    return 1;
}