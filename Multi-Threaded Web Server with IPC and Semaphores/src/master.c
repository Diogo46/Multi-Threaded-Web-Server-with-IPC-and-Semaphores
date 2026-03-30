// master.c - Master process implementation
#include "master.h"
#include "shared_mem.h"
#include "semaphores.h"
#include "worker.h"
#include "http.h"
#include "stats.h" 
#include "logger.h"
#include "ipc_fd.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

/* SO_REUSEPORT may not be defined on older systems */
#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

// --- Periodic Statistics Printer Thread ---
void* stats_printer_thread(void* arg) {
    struct {
        shared_data_t* data;
        semaphores_t* sems;
    } *ctx = arg;

    while (keep_running) {
        sleep(30);  // Update every 30 seconds

        sem_wait(ctx->sems->stats_mutex);

        // Read values from shared memory
        long total = ctx->data->stats.total_requests;
        long bytes = ctx->data->stats.bytes_transferred;
        int active = ctx->data->stats.active_connections;
        double avg_time = ctx->data->stats.avg_response_time;
        time_t start = ctx->data->stats.start_time;

        // Aggregate status codes from the histogram
        long ok2xx = 0, err4xx = 0, err5xx = 0;
        for (int i = 200; i < 300; i++) ok2xx += ctx->data->stats.status_counts[i];
        for (int i = 400; i < 500; i++) err4xx += ctx->data->stats.status_counts[i];
        for (int i = 500; i < 600; i++) err5xx += ctx->data->stats.status_counts[i];

        sem_post(ctx->sems->stats_mutex);

        // Calculate Uptime
        time_t now = time(NULL);
        long uptime = now - start;

        // Print Formatted Output
        printf("\n=== SERVER STATISTICS ===\n");
        printf("Uptime: %ld seconds\n", uptime);
        printf("Total Requests: %ld\n", total);
        printf("Successful (2xx): %ld\n", ok2xx);
        printf("Client Errors (4xx): %ld\n", err4xx);
        printf("Server Errors (5xx): %ld\n", err5xx);
        printf("Bytes Transferred: %ld\n", bytes);
        printf("Active Connections: %d\n", active);

        /* Cache metrics */
        sem_wait(ctx->sems->stats_mutex);
        uint64_t hits = ctx->data->stats.cache_hits;
        uint64_t misses = ctx->data->stats.cache_misses;
        sem_post(ctx->sems->stats_mutex);
        printf("Cache Hits: %lu\n", (unsigned long)hits);
        printf("Cache Misses: %lu\n", (unsigned long)misses);
        
        /* Calculate cache hit rate percentage */
        double hit_rate = 0.0;
        if ((hits + misses) > 0) {
            hit_rate = (100.0 * hits) / (hits + misses);
        }
        printf("Cache Hit Rate: %.1f%%\n", hit_rate);

        printf("Avg Response Time: %.2f ms\n", avg_time);
        printf("=========================\n\n");
        fflush(stdout);
    }
    free(ctx);
    return NULL;
}

// --- Rest of Master Code ---

void signal_handler(int signum) {
    (void)signum;
    keep_running = 0;
}

int create_server_socket(int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Allow multiple binds (helps during rapid restarts) */
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    /* Increase backlog to avoid transient connection refusals under load */
    if (listen(sockfd, 1024) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

void enqueue_connection(shared_data_t* data, semaphores_t* sems, int client_fd, int worker_id) {
    if (worker_id < 0 || worker_id >= sems->num_workers) {
        close(client_fd);
        return;
    }

    if (sem_trywait(sems->empty_slots[worker_id]) != 0) {
        const char *body = "503 Service Unavailable\n";
        ssize_t bytes = send_http_response(client_fd, 503, "Service Unavailable", "text/plain", body, strlen(body));

        sem_wait(sems->stats_mutex);
        data->stats.total_requests++;
        if (503 < 600) data->stats.status_counts[503]++;
        data->stats.bytes_transferred += (bytes > 0 ? bytes : 0);
        sem_post(sems->stats_mutex);

        log_request(sems->log_mutex, "-", "-", "-", 503, (bytes > 0 ? (size_t)bytes : 0), "-", "-");
        close(client_fd);
        return;
    }

    sem_wait(sems->queue_mutex[worker_id]);
    connection_queue_t *q = &data->worker_queues[worker_id];
    q->tokens[q->rear] = 1; // placeholder token
    q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
    q->count++;
    sem_post(sems->queue_mutex[worker_id]);

    sem_post(sems->filled_slots[worker_id]);
}

/* MASTER PROCESS MAIN LOOP: accept incoming connections and distribute to workers via round-robin.
   the master never processes requests directly; it acts as a dispatcher/load balancer.
   critical: the master is single-threaded, so accept() is the bottleneck under heavy load.
   
   QUEUE MANAGEMENT: each worker has bounded queue of size MAX_QUEUE_SIZE (100).
   if all workers' queues are full (400 pending total), new clients get 503 Service Unavailable.
   this bounds memory usage and response latency (prevents unbounded queuing). */
void run_master(shared_data_t* data, semaphores_t* sems, int port, int* master_socks, int num_workers) {
    int server_fd = create_server_socket(port);
    if (server_fd < 0) {
        perror("Failed to create server socket");
        exit(1);
    }

    printf("[MASTER] Listening on port %d...\n", port);

    /* Initialize stats start time (if not already done) */
    if (data->stats.start_time == 0) {
        data->stats.start_time = time(NULL);
    }

    /* Start stats printer thread: periodically dump aggregated statistics */
    pthread_t stats_thread;
    struct { shared_data_t* data; semaphores_t* sems; } *ctx = malloc(sizeof(struct { shared_data_t* data; semaphores_t* sems; }));
    ctx->data = data;
    ctx->sems = sems;
    pthread_create(&stats_thread, NULL, stats_printer_thread, ctx);

    fflush(stdout);

    /* ROUND-ROBIN DISTRIBUTION: rr counter cycles through workers [0, num_workers) */
    int rr = 0;
    while (keep_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        /* ACCEPT BLOCKING: waits here until client connects.
           one accept() per request (master is single-threaded bottleneck). */
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!keep_running) break;
            perror("[MASTER] accept() failed");
            continue;
        }

        if (!keep_running) {
            close(client_fd);
            break;
        }

        /* SELECT NEXT WORKER: round-robin ensures fair distribution */
        int worker = rr % num_workers;
        rr++;

        /* TRY TO ENQUEUE: sem_trywait (non-blocking) checks if worker queue has space.
           if full, try other workers in sequence, then return 503 if all full. */
        if (sem_trywait(sems->empty_slots[worker]) != 0) {
            /* primary worker queue full; search for alternate worker */
            int tried = 0;
            int chosen = -1;
            for (int i = 0; i < num_workers; i++) {
                int w = (worker + i) % num_workers;
                if (sem_trywait(sems->empty_slots[w]) == 0) { chosen = w; break; }
                tried++;
            }
            if (chosen == -1) {
                /* ALL WORKERS QUEUES FULL: return 503 Service Unavailable */
                const char *body = "503 Service Unavailable\n";
                ssize_t bytes = send_http_response(client_fd, 503, "Service Unavailable", "text/plain", body, strlen(body));

                sem_wait(sems->stats_mutex);
                data->stats.total_requests++;
                if (503 < 600) data->stats.status_counts[503]++;
                data->stats.bytes_transferred += (bytes > 0 ? bytes : 0);
                sem_post(sems->stats_mutex);

                log_request(sems->log_mutex, "-", "-", "-", 503, (bytes > 0 ? (size_t)bytes : 0), "-", "-");
                close(client_fd);
                continue;
            } else {
                worker = chosen;
            }
        }

        /* ENQUEUE TOKEN: add placeholder token to worker's connection queue */
        sem_wait(sems->queue_mutex[worker]);
        connection_queue_t *q = &data->worker_queues[worker];
        q->tokens[q->rear] = 1;
        q->rear = (q->rear + 1) % MAX_QUEUE_SIZE;
        q->count++;
        sem_post(sems->queue_mutex[worker]);

        /* PASS FD TO WORKER: send client socket fd via unix domain socket (SCM_RIGHTS).
           this is zero-copy: kernel duplicates fd in worker's table, no data copied. */
        if (master_socks && master_socks[worker] >= 0) {
            if (send_fd(master_socks[worker], client_fd) < 0) {
                /* FD SEND FAILED: roll back queue entry and return 500 */
                sem_wait(sems->queue_mutex[worker]);
                q->rear = (q->rear - 1 + MAX_QUEUE_SIZE) % MAX_QUEUE_SIZE;
                q->count--;
                sem_post(sems->queue_mutex[worker]);
                sem_post(sems->empty_slots[worker]);

                const char *body = "500 Internal Server Error\n";
                ssize_t bytes = send_http_response(client_fd, 500, "Internal Server Error", "text/plain", body, strlen(body));
                sem_wait(sems->stats_mutex);
                data->stats.total_requests++;
                if (500 < 600) data->stats.status_counts[500]++;
                data->stats.bytes_transferred += (bytes > 0 ? bytes : 0);
                sem_post(sems->stats_mutex);
                log_request(sems->log_mutex, "-", "-", "-", 500, (bytes > 0 ? (size_t)bytes : 0), "-", "-");
                close(client_fd);
                continue;
            }
            /* FD OWNERSHIP TRANSFERRED: master closes its copy, worker has its own */
            close(client_fd);
        }

        /* SIGNAL WORKER: post filled_slots semaphore to wake dequeue_token() in worker */
        sem_post(sems->filled_slots[worker]);
    }

    close(server_fd);
    keep_running = 0;
    pthread_join(stats_thread, NULL);
}