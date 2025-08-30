#include "help.h"
#include <stdio.h>

int help() {
    fprintf(stderr,"Usage: ./latte-server [/path/to/latte.conf] [options] [-]\n");
    fprintf(stderr,"       ./latte-server - (read config from stdin)\n");
    fprintf(stderr,"       ./latte-server -v or --version\n");
    fprintf(stderr,"       ./latte-server -h or --help\n");
    fprintf(stderr,"       ./latte-server --test-memory <megabytes>\n");
    fprintf(stderr,"       ./latte-server --check-system\n");
    fprintf(stderr,"\n");
    fprintf(stderr,"Examples:\n");
    fprintf(stderr,"       ./latte-server (run the server with default conf)\n");
    fprintf(stderr,"       echo 'maxmemory 128mb' | ./latte-server -\n");
    fprintf(stderr,"       ./latte-server /etc/latte/6379.conf\n");
    fprintf(stderr,"       ./latte-server --port 7777\n");
    fprintf(stderr,"       ./latte-server --port 7777 --replicaof 127.0.0.1 8888\n");
    fprintf(stderr,"       ./latte-server /etc/mylatte.conf --loglevel verbose -\n");
    fprintf(stderr,"       ./latte-server /etc/mylatte.conf --loglevel verbose\n\n");
    fprintf(stderr,"Sentinel mode:\n");
    fprintf(stderr,"       ./latte-server /etc/sentinel.conf --sentinel\n");
    return 1;
}