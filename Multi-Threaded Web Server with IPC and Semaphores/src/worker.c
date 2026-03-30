#define _XOPEN_SOURCE 500

#include "worker.h"
#include "http.h"
#include "logger.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "thread_pool.h"
#include "config.h"
#include "ipc_fd.h"
#include "lru_cache.h"
#include "cgi.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/sendfile.h>
#include <limits.h>
#include <strings.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>

/* worker communication socket (set in main child before run_worker) */
int worker_comm_fd = -1;

// Fallback if path_max is not defined
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Global shutdown flag
extern volatile sig_atomic_t keep_running;

/*
 * Helper to send custom error pages (Requirement: Custom Error Pages)
 * Tries to open errors/<status>.html. Falls back to plain text if missing.
 */
ssize_t send_error_response(int fd, int status, const char* msg) {
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "errors/%d.html", status);

    int file_fd = open(filepath, O_RDONLY);
    if (file_fd >= 0) {
        // Found custom error page
        struct stat st;
        if (fstat(file_fd, &st) == 0) {
            char header[1024];
            int header_len = snprintf(header, sizeof(header),
                "HTTP/1.1 %d %s\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %zu\r\n"
                "Server: ConcurrentHTTP/1.0\r\n"
                "Connection: keep-alive\r\n"
                "\r\n",
                status, msg, (size_t)st.st_size
            );
            
            // Send header
            send(fd, header, header_len, 0);
            
            // Send file content
            off_t offset = 0;
            sendfile(fd, file_fd, &offset, st.st_size);
            ssize_t total = header_len + st.st_size;
            close(file_fd);
            return total;
        }
        close(file_fd);
    } else {
        // Fallback: Send simple text response
        char body[256];
        snprintf(body, sizeof(body), "%d %s\n", status, msg);
        return send_http_response(fd, status, msg, "text/plain", body, strlen(body));
    }

    return 0;
}

int dequeue_token(shared_data_t* data, semaphores_t* sems, int worker_id) {
    if (worker_id < 0 || worker_id >= sems->num_workers) return -1;

    if (sem_wait(sems->filled_slots[worker_id]) != 0) return -1;

    sem_wait(sems->queue_mutex[worker_id]);
    connection_queue_t *q = &data->worker_queues[worker_id];
    int token = q->tokens[q->front];
    q->front = (q->front + 1) % MAX_QUEUE_SIZE;
    q->count--;
    sem_post(sems->queue_mutex[worker_id]);

    sem_post(sems->empty_slots[worker_id]);
    return token;
}

/* Per-worker cache instance (one per process). */
static lru_cache_t *worker_cache = NULL;

static void get_client_ip_str(int fd, char* buf, size_t buflen) {
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    if (getpeername(fd, (struct sockaddr*)&addr, &len) == 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in* sin = (struct sockaddr_in*)&addr;
            inet_ntop(AF_INET, &sin->sin_addr, buf, buflen);
            return;
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6* sin6 = (struct sockaddr_in6*)&addr;
            inet_ntop(AF_INET6, &sin6->sin6_addr, buf, buflen);
            return;
        }
    }

    if (buflen > 0) {
        snprintf(buf, buflen, "unknown");
    }
}

static void process_client(int client_fd, shared_data_t* data, semaphores_t* sems) {
    char request_buffer[4096];
    char client_ip[INET6_ADDRSTRLEN];
    int bytes_read;
    int status_code = 500;
    off_t bytes_sent = 0;
    char method[16] = "";
    char path[PATH_MAX] = "";
    char http_version[16] = "";
    const char *referer = "-";
    const char *user_agent = "-";

    // Load server configuration
    server_config_t server_config;
    if (load_config("server.conf", &server_config) != 0) {
        // Use default if config load fails
        strcpy(server_config.document_root, "./www");
    }

    // Apply per-connection timeout from configuration.
    struct timeval tv;
    tv.tv_sec = server_config.timeout_seconds;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Track active connections
    sem_wait(sems->stats_mutex);
    data->stats.active_connections++;
    sem_post(sems->stats_mutex);

    get_client_ip_str(client_fd, client_ip, sizeof(client_ip));

    /* KEEP-ALIVE LOOP: HTTP/1.1 persistent connection support.
       single TCP connection handles multiple back-to-back requests.
       reduces connection overhead: no TCP 3-way handshake per request.
       socket has SO_RCVTIMEO set (from server config) so idle connections auto-close after TIMEOUT_SECONDS. */
    while (1) {
        /* RESET STATE FOR EACH REQUEST: each request in loop is independent */
        bytes_sent = 0;
        status_code = 500;
        strcpy(method, "");
        strcpy(path, "");
        strcpy(http_version, "");
        referer = "-";
        user_agent = "-";

        /* START TIMER: CLOCK_MONOTONIC is unaffected by system time adjustments,
           ideal for measuring response latency accurately. */
        struct timespec start_time, end_time;
        clock_gettime(CLOCK_MONOTONIC, &start_time);

        /* READ HTTP REQUEST LINE + HEADERS: blocking read with timeout (SO_RCVTIMEO)
           socket will timeout if client silent for TIMEOUT_SECONDS (default 30). */
        bytes_read = read(client_fd, request_buffer, sizeof(request_buffer) - 1);
        
        /* KEEP-ALIVE LOOP EXIT: CLIENT DISCONNECTED GRACEFULLY
           bytes_read == 0 signals end-of-file: client closed connection cleanly (FIN). */
        if (bytes_read == 0) {
            break; /* exit keep-alive loop, close socket below */
        }

        /* KEEP-ALIVE LOOP EXIT: READ ERROR OR TIMEOUT
           bytes_read < 0 indicates error (ETIMEDOUT from SO_RCVTIMEO, connection reset, etc.). */
        if (bytes_read < 0) {
            break; /* exit keep-alive loop on read error */
        }

        /* NULL-TERMINATE REQUEST BUFFER for safe string operations */
        request_buffer[bytes_read] = '\0';

    // Parse request line
    if (sscanf(request_buffer, "%15s %4095s %15s", method, path, http_version) != 3) {
        bytes_sent = send_error_response(client_fd, 400, "Bad Request");
        status_code = 400;
        goto update_stats;
    }

    /* Parse headers for Referer and User-Agent (simple linear scan). */
    char *line = request_buffer;
    while (line && *line) {
        char *next = strstr(line, "\r\n");
        if (next) { *next = '\0'; next += 2; }
        if (strncasecmp(line, "Referer:", 8) == 0) {
            char *val = line + 8;
            while (*val == ' ' || *val == '\t') val++;
            referer = (*val) ? val : "-";
        } else if (strncasecmp(line, "User-Agent:", 11) == 0) {
            char *val = line + 11;
            while (*val == ' ' || *val == '\t') val++;
            user_agent = (*val) ? val : "-";
        }
        line = next;
    }

    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
        bytes_sent = send_error_response(client_fd, 405, "Method Not Allowed");
        status_code = 405;
        goto update_stats;
    }

    // Extract and save query string before removing it
    char query_string[2048] = "";
    char* query = strchr(path, '?');
    if (query) {
        strncpy(query_string, query + 1, sizeof(query_string) - 1);
        query_string[sizeof(query_string) - 1] = '\0';
        *query = '\0';  /* Now remove query string from path */
    }

    // Security check: Directory traversal
    if (strstr(path, "..") != NULL) {
        bytes_sent = send_error_response(client_fd, 403, "Forbidden");
        status_code = 403;
        goto update_stats;
    }

    // Handle root path
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    // Build full file path
    char full_path[PATH_MAX];
    size_t root_len = strlen(server_config.document_root);
    size_t path_len = strlen(path);
    if (root_len + path_len >= PATH_MAX) {
        bytes_sent = send_error_response(client_fd, 414, "URI Too Long");
        status_code = 414;
        goto update_stats;
    }
    
    // SAFE REPLACEMENT:
    // We already checked lengths above, so strcpy/strcat are safe and won't trigger -Wformat-truncation
    strcpy(full_path, server_config.document_root);
    strcat(full_path, path);

    // Check if directory, append index.html
    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        strncat(full_path, "/index.html", sizeof(full_path) - strlen(full_path) - 1);
    }

    /* Check for special API endpoints before serving static files */
    if (strcmp(path, "/api/stats") == 0) {
        /* Return JSON statistics (read-only access, no lock needed) */
        size_t json_len = 0;
        char* json_body = generate_stats_json(&data->stats, &json_len);
        
        if (json_body) {
            bytes_sent = send_http_response(client_fd, 200, "OK", "application/json", 
                                           json_body, json_len);
            free(json_body);
            status_code = 200;
            goto update_stats;
        } else {
            bytes_sent = send_error_response(client_fd, 500, "Internal Server Error");
            status_code = 500;
            goto update_stats;
        }
    }

    /* Check cache for GET (only body is cached). For HEAD, we'll still report headers but not cache body.
       Skip cache for CGI scripts. */
    const char* mime_type = get_mime_type(full_path);

    if (server_config.cache_enabled && strcasecmp(method, "GET") == 0 && worker_cache && !is_cgi_script(full_path)) {
        void *copy_buf = NULL;
        size_t copy_len = 0;
        int hit = lru_cache_get_copy(worker_cache, full_path, &copy_buf, &copy_len);
        sem_wait(sems->stats_mutex);
        if (hit == 1) {
            data->stats.cache_hits++;
        } else if (hit == 0) {
            data->stats.cache_misses++;
        }
        sem_post(sems->stats_mutex);

        /* Lightweight runtime debug logging for cache activity. */
        if (hit == 1) {
            printf("[CACHE] HIT %s\n", full_path);
            fflush(stdout);
        } else if (hit == 0) {
            printf("[CACHE] MISS %s\n", full_path);
            fflush(stdout);
        }

        if (hit == 1) {
            /* Send header */
            char header[1024];
            int header_len = snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %zu\r\n"
                "Server: ConcurrentHTTP/1.0\r\n"
                "Connection: keep-alive\r\n"
                "\r\n",
                mime_type, copy_len
            );
            if (send(client_fd, header, header_len, 0) < 0) {
                free(copy_buf);
                status_code = 500;
                bytes_sent = 0;
                goto update_stats;
            }
            if (copy_buf && send(client_fd, copy_buf, copy_len, 0) > 0) {
                bytes_sent = header_len + (off_t)copy_len;
            } else {
                bytes_sent = header_len;
            }
            free(copy_buf);
            status_code = 200;
            goto update_stats;
        }
    }

    /* Not served from cache: open file */
    int file_fd = open(full_path, O_RDONLY);
    if (file_fd < 0) {
        bytes_sent = send_error_response(client_fd, 404, "Not Found");
        status_code = 404;
        goto update_stats;
    }

    if (fstat(file_fd, &st) < 0) {
        close(file_fd);
        bytes_sent = send_error_response(client_fd, 500, "Internal Server Error");
        status_code = 500;
        goto update_stats;
    }

    /* Check if this is a CGI script */
    if (is_cgi_script(full_path)) {
        close(file_fd);  /* Don't need file_fd for CGI execution */
        
        /* Verify CGI script exists and is executable */
        if (access(full_path, X_OK) != 0) {
            bytes_sent = send_error_response(client_fd, 403, "Forbidden");
            status_code = 403;
            goto update_stats;
        }

        /* Execute CGI script */
        size_t cgi_output_len = 0;
        char* cgi_output = execute_cgi_script(full_path, method, query_string, client_fd, 0, &cgi_output_len);
        
        if (cgi_output == NULL) {
            bytes_sent = send_error_response(client_fd, 500, "Internal Server Error");
            status_code = 500;
            goto update_stats;
        }

        /* Send CGI output as HTML response */
        char header[1024];
        int header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: %zu\r\n"
            "Server: ConcurrentHTTP/1.0\r\n"
            "Connection: keep-alive\r\n"
            "\r\n",
            cgi_output_len
        );

        if (send(client_fd, header, header_len, 0) < 0) {
            free(cgi_output);
            status_code = 500;
            bytes_sent = 0;
            goto update_stats;
        }

        if (send(client_fd, cgi_output, cgi_output_len, 0) > 0) {
            bytes_sent = header_len + (off_t)cgi_output_len;
        } else {
            bytes_sent = header_len;
        }

        free(cgi_output);
        status_code = 200;
        goto update_stats;
    }

    /* Decide whether to load into memory for caching or use sendfile */
    int should_cache = server_config.cache_enabled && st.st_size <= server_config.cache_max_file_size && worker_cache != NULL;

    if (should_cache && strcasecmp(method, "GET") == 0) {
        void *buf = NULL;
        size_t len = 0;
        close(file_fd); /* load_file_to_memory will reopen */
        if (load_file_to_memory(full_path, &buf, &len) == 0) {
            /* Send header */
            char header[1024];
            int header_len = snprintf(header, sizeof(header),
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Content-Length: %zu\r\n"
                "Server: ConcurrentHTTP/1.0\r\n"
                "Connection: keep-alive\r\n"
                "\r\n",
                mime_type, len
            );
            if (send(client_fd, header, header_len, 0) < 0) {
                free(buf);
                status_code = 500;
                bytes_sent = 0;
                goto update_stats;
            }

            if (send(client_fd, buf, len, 0) > 0) {
                bytes_sent = header_len + (off_t)len;
            } else {
                bytes_sent = header_len;
            }

            /* Try to insert into cache (best-effort) */
            if (worker_cache) lru_cache_put(worker_cache, full_path, buf, len);
            free(buf);
            status_code = 200;
            goto update_stats;
        } else {
            /* failed to load into memory; fall back to sendfile path */
            file_fd = open(full_path, O_RDONLY);
            if (file_fd < 0) {
                bytes_sent = send_error_response(client_fd, 500, "Internal Server Error");
                status_code = 500;
                goto update_stats;
            }
            /* continue to sendfile below */
        }
    }

    /* Non-cached path: use sendfile (supports HEAD semantics) */
    const char* mime = mime_type;

    char header[1024];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Server: ConcurrentHTTP/1.0\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        mime, (size_t)st.st_size
    );

    if (send(client_fd, header, header_len, 0) < 0) {
        close(file_fd);
        status_code = 500;
        bytes_sent = 0;
        goto update_stats;
    }

    if (strcasecmp(method, "HEAD") != 0) {
        off_t offset = 0;
        ssize_t sent = sendfile(client_fd, file_fd, &offset, st.st_size);
        if (sent > 0) {
            bytes_sent = header_len + sent;
        } else {
            bytes_sent = header_len;
        }
    } else {
        bytes_sent = header_len;
    }

    close(file_fd);
    status_code = 200;

update_stats:
    // 2. Stop Timer
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0;
    elapsed_ms += (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

    // Update shared statistics atomically
    sem_wait(sems->stats_mutex);
    
    data->stats.total_requests++;
    data->stats.bytes_transferred += bytes_sent;

    // Update status code histogram (0-599)
    if (status_code >= 0 && status_code < 600) {
        data->stats.status_counts[status_code]++;
    }

    // Calculate Average Response Time (Rolling Average)
    double old_avg = data->stats.avg_response_time;
    if (data->stats.total_requests == 1) {
        data->stats.avg_response_time = elapsed_ms;
    } else {
        data->stats.avg_response_time = old_avg + (elapsed_ms - old_avg) / data->stats.total_requests;
    }

    sem_post(sems->stats_mutex);

    log_request(sems->log_mutex, client_ip, method, path, status_code, bytes_sent, referer, user_agent);
    
    } /* End of keep-alive loop */
    
    /* Clean up connection */
    sem_wait(sems->stats_mutex);
    if (data->stats.active_connections > 0) {
        data->stats.active_connections--;
    }
    sem_post(sems->stats_mutex);
    close(client_fd);
}

void run_worker(shared_data_t* data, semaphores_t* sems, int worker_id) {
    printf("[WORKER %d] started\n", worker_id);
    fflush(stdout);

    /* create per-worker thread pool */
    server_config_t cfg;
    load_config("server.conf", &cfg);
    /* Initialize per-worker cache if enabled */
    if (cfg.cache_enabled) {
        worker_cache = lru_cache_create(cfg.cache_max_bytes, cfg.cache_max_file_size);
        if (!worker_cache) {
            /* failed to create cache; continue without cache */
            worker_cache = NULL;
        }
    }
    int threads_per_worker = cfg.threads_per_worker > 0 ? cfg.threads_per_worker : 4;
    thread_pool_t* pool = create_thread_pool(threads_per_worker, process_client);
    if (!pool) {
        fprintf(stderr, "[WORKER %d] failed to create thread pool\n", worker_id);
        if (worker_cache) { lru_cache_free(worker_cache); worker_cache = NULL; }
        return;
    }

    while (keep_running) {
        int tk = dequeue_token(data, sems, worker_id);
        if (tk < 0) continue;

        // receive the actual FD from master over unix socket
        int client_fd = -1;
        if (worker_comm_fd >= 0) {
            client_fd = recv_fd(worker_comm_fd);
        }

        if (client_fd < 0) {
            // failed to receive FD; continue
            continue;
        }

        if (thread_pool_submit(pool, client_fd, data, sems) != 0) {
            close(client_fd);
        }
    }

    destroy_thread_pool(pool);

    /* Destroy per-worker cache */
    if (worker_cache) {
        lru_cache_free(worker_cache);
        worker_cache = NULL;
    }

    printf("[WORKER %d] shutting down\n", worker_id);
    fflush(stdout);
}