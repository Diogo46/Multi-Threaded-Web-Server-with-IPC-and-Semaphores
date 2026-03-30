#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

void log_request(sem_t* log_sem,
                 const char* client_ip,
                 const char* method,
                 const char* path,
                 int status,
                 size_t bytes,
                 const char* referer,
                 const char* user_agent)
{
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);

    char timestamp[64];
    strftime(timestamp, sizeof(timestamp),
             "%d/%b/%Y:%H:%M:%S %z",
             tm_info);

    sem_wait(log_sem);

    /* simple log rotation at ~10MB */
    struct stat st;
    if (stat("access.log", &st) == 0 && st.st_size > 10 * 1024 * 1024) {
        rename("access.log", "access.log.1");
    }

    FILE* log = fopen("access.log", "a");
    if (log) {
        /* Apache Combined Log Format */
        fprintf(log,
                "%s - - [%s] \"%s %s HTTP/1.1\" %d %zu \"%s\" \"%s\"\n",
                client_ip,
                timestamp,
                method,
                path,
                status,
                bytes,
                referer ? referer : "-",
                user_agent ? user_agent : "-");
        fclose(log);
    }

    sem_post(log_sem);
}
