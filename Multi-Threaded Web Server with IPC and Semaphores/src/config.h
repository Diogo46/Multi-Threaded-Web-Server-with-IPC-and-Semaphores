// config.h
#ifndef CONFIG_H
#define CONFIG_H

typedef struct {
    int port;
    char document_root[256];
    int num_workers;
    int threads_per_worker;
    int max_queue_size;
    char log_file[256];
    /* Legacy: cache size in MB (kept for backward compat). New fields below control cache behavior. */
    int cache_size_mb;
    /* New cache configuration */
    int cache_enabled;           /* 0 = disabled, 1 = enabled */
    int cache_max_bytes;         /* total bytes per-worker to use for cache */
    int cache_max_file_size;     /* don't cache files larger than this */
    int timeout_seconds;
} server_config_t;

/* Global config instance filled by load_config() */
extern server_config_t server_config;

/* Load config file into provided struct (also updates global `server_config`).
 * Returns 0 on success, -1 on failure. */
int load_config(const char* filename, server_config_t* config);

#endif
