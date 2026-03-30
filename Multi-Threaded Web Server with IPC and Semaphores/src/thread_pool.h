// thread_pool.h
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include "shared_mem.h"
#include "semaphores.h"

typedef struct {
    int client_fd;
    shared_data_t* data;
    semaphores_t* sems;
} work_item_t;

typedef struct {
    pthread_t* threads;
    int num_threads;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;

    /* circular queue of work items */
    work_item_t* queue;
    int q_head;
    int q_tail;
    int q_count;
    int q_capacity;

    /* worker function to process a work_item */
    void (*worker_func)(int client_fd, shared_data_t* data, semaphores_t* sems);
} thread_pool_t;

/* Create a thread pool with `num_threads`. `worker_func` is called by
 * each pool thread with (client_fd, data, sems) when a work item is dequeued.
 */
thread_pool_t* create_thread_pool(int num_threads, void (*worker_func)(int, shared_data_t*, semaphores_t*));

/* Submit a client fd to the pool. This function blocks if the pool queue
 * is full. Returns 0 on success, -1 on failure (e.g., pool is shutting down).
 */
int thread_pool_submit(thread_pool_t* pool, int client_fd, shared_data_t* data, semaphores_t* sems);

/* Destroy the pool: signal shutdown and join threads. */
void destroy_thread_pool(thread_pool_t* pool);

#endif
