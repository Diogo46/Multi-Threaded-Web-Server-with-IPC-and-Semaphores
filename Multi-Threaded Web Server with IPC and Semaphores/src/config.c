// config.c
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Global config instance with sensible defaults */
server_config_t server_config = {
    .port = 8080,
    .document_root = "./www",
    .num_workers = 4,
    .threads_per_worker = 10,
    .max_queue_size = 100,
    .log_file = "access.log",
    .cache_size_mb = 0,
    .cache_enabled = 1,
    .cache_max_bytes = 10 * 1024 * 1024, /* 10 MB per README */
    .cache_max_file_size = 1 * 1024 * 1024, /* cap per-file caching */
    .timeout_seconds = 30
};

int load_config(const char* filename, server_config_t* config) {
    /* Start with defaults from global */
    if (config) memcpy(config, &server_config, sizeof(server_config_t));

    FILE* fp = fopen(filename, "r");
    if (!fp) {
        /* missing config is not fatal; keep defaults */
        if (config) memcpy(&server_config, config, sizeof(server_config_t));
        return -1;
    }

    char line[512];
    char* eq;

    while (fgets(line, sizeof(line), fp)) {
        /* trim leading whitespace */
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = p;
        char* value = eq + 1;

        /* trim trailing whitespace/newline on value */
        char* vend = value + strlen(value) - 1;
        while (vend > value && (*vend == '\n' || *vend == '\r' || *vend == ' ' || *vend == '\t')) { *vend = '\0'; vend--; }

        if (strcmp(key, "PORT") == 0) server_config.port = atoi(value);
        else if (strcmp(key, "DOCUMENT_ROOT") == 0) strncpy(server_config.document_root, value, sizeof(server_config.document_root)-1);
        else if (strcmp(key, "NUM_WORKERS") == 0) server_config.num_workers = atoi(value);
        else if (strcmp(key, "THREADS_PER_WORKER") == 0) server_config.threads_per_worker = atoi(value);
        else if (strcmp(key, "MAX_QUEUE_SIZE") == 0) server_config.max_queue_size = atoi(value);
        else if (strcmp(key, "LOG_FILE") == 0) strncpy(server_config.log_file, value, sizeof(server_config.log_file)-1);
        else if (strcmp(key, "CACHE_SIZE_MB") == 0) {
            server_config.cache_size_mb = atoi(value);
            if (server_config.cache_size_mb > 0) {
                server_config.cache_max_bytes = server_config.cache_size_mb * 1024 * 1024;
            }
        }
        else if (strcmp(key, "CACHE_ENABLED") == 0) server_config.cache_enabled = atoi(value);
        else if (strcmp(key, "CACHE_MAX_BYTES") == 0) server_config.cache_max_bytes = atoi(value);
        else if (strcmp(key, "CACHE_MAX_FILE_SIZE") == 0) server_config.cache_max_file_size = atoi(value);
        else if (strcmp(key, "TIMEOUT_SECONDS") == 0) server_config.timeout_seconds = atoi(value);
    }

    fclose(fp);

    /* Environment overrides (if set) */
    const char *env_port = getenv("HTTP_PORT");
    if (env_port) server_config.port = atoi(env_port);
    const char *env_workers = getenv("HTTP_WORKERS");
    if (env_workers) server_config.num_workers = atoi(env_workers);
    const char *env_threads = getenv("HTTP_THREADS");
    if (env_threads) server_config.threads_per_worker = atoi(env_threads);
    const char *env_timeout = getenv("HTTP_TIMEOUT");
    if (env_timeout) server_config.timeout_seconds = atoi(env_timeout);

    if (config) memcpy(config, &server_config, sizeof(server_config_t));
    return 0;
}
