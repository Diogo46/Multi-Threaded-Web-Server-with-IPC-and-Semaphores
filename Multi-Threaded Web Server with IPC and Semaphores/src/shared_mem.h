#ifndef SHARED_MEM_H
#define SHARED_MEM_H

#include <sys/types.h>
#include <time.h>

#define MAX_QUEUE_SIZE 256
#define MAX_WORKERS 16

#include "stats.h"

typedef struct {
    int tokens[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int count;
} connection_queue_t;

typedef struct {
    connection_queue_t worker_queues[MAX_WORKERS];
    server_stats_t stats;
    volatile int shutdown; // Flag to signal workers to stop
} shared_data_t;

shared_data_t* create_shared_memory();
void destroy_shared_memory(shared_data_t* data);

#endif