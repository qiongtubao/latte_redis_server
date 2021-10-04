#include "log.h"
#include <stdarg.h>
#include <sys/uio.h>
#include <time.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#include <syslog.h>

int syslog_enabled;
char* logfile;
/* Low level logging. To use only for very big messages, otherwise
 * serverLog() is to prefer. */
void serverLogRaw(int level, const char *msg) {
    const int syslogLevelMap[] = { LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING };
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & LL_RAW);
    int log_to_stdout = logfile[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < verbosity) return;

    fp = log_to_stdout ? stdout : fopen(logfile,"a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        int off;
        struct timeval tv;
        int role_char;
        // pid_t pid = getpid();

        gettimeofday(&tv,NULL);
        struct tm tm;
        // nolocks_localtime(&tm,tv.tv_sec,server.timezone,server.daylight_active);
        off = strftime(buf,sizeof(buf),"%d %b %Y %H:%M:%S.",&tm);
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        // if (server.sentinel_mode) {
        //     role_char = 'X'; /* Sentinel. */
        // } else if (pid != server.pid) {
        //     role_char = 'C'; /* RDB / AOF writing child. */
        // } else {
        //     role_char = (server.masterhost ? 'S':'M'); /* Slave or Master. */
        // }
        fprintf(fp,"%d:%c %s %c %s\n",
            (int)getpid(),role_char, buf,c[level],msg);
    }
    fflush(fp);

    if (!log_to_stdout) fclose(fp);
    if (syslog_enabled) syslog(syslogLevelMap[level], "%s", msg);
}

/* Like serverLogRaw() but with printf-alike support. This is the function that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void _serverLog(int level, const char *fmt, ...) {
    va_list ap;
    char msg[LOG_MAX_LEN];

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    serverLogRaw(level,msg);
}