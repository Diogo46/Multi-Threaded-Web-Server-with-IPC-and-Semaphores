// thread_pool.c
#include "thread_pool.h"
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

/* WORKER THREAD MAIN LOOP: classical bounded producer-consumer pattern with condition variables.
   avoids busy-waiting (spinning); threads sleep on condition until work arrives or shutdown occurs.
   this is far more efficient than polling, especially with 10 threads per worker checking empty queues. */
static void* worker_thread(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        /* WAITING PHASE: sleep until work arrives or shutdown signal received.
           condition variable atomically releases mutex while sleeping, reacquiring on wakeup.
           loop continues while both conditions hold: not shutdown AND queue is empty. */
        while (!pool->shutdown && pool->q_count == 0) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        /* SHUTDOWN CHECK: if shutdown requested AND queue empty, exit gracefully */
        if (pool->shutdown && pool->q_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* DEQUEUE PHASE: pop one item from circular buffer using front pointer */
        work_item_t item = pool->queue[pool->q_head];
        pool->q_head = (pool->q_head + 1) % pool->q_capacity;  /* wrap around at buffer boundary */
        pool->q_count--;  /* decrement occupancy */

        /* NOTIFY PRODUCERS: wake any threads blocked waiting for buffer space */
        pthread_cond_broadcast(&pool->cond);

        pthread_mutex_unlock(&pool->mutex);

        /* PROCESS ITEM OUTSIDE LOCK: critical to minimize lock contention.
           while this thread is processing a client, other threads can enqueue/dequeue.
           this separation of concerns (lock for queue manipulation, unlock for processing)
           is essential for scaling to 10 threads per worker. */
        if (pool->worker_func) {
            pool->worker_func(item.client_fd, item.data, item.sems);
        } else {
            /* fallback: if no handler registered, close fd to prevent file descriptor leak */
            close(item.client_fd);
        }
    }

    return NULL;
}

thread_pool_t* create_thread_pool(int num_threads, void (*worker_func)(int, shared_data_t*, semaphores_t*)) {
    thread_pool_t* pool = malloc(sizeof(thread_pool_t));
    if (!pool) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * num_threads);
    if (!pool->threads) { free(pool); return NULL; }

    pool->num_threads = num_threads;
    pool->shutdown = 0;
    pool->worker_func = worker_func;

    /* queue capacity: allow some buffering per thread */
    pool->q_capacity = num_threads * 16;
    if (pool->q_capacity < 64) pool->q_capacity = 64;
    pool->queue = calloc(pool->q_capacity, sizeof(work_item_t));
    pool->q_head = pool->q_tail = pool->q_count = 0;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&pool->threads[i], NULL, worker_thread, pool);
    }

    return pool;
}

int thread_pool_submit(thread_pool_t* pool, int client_fd, shared_data_t* data, semaphores_t* sems) {
    if (!pool) return -1;

    pthread_mutex_lock(&pool->mutex);
    while (!pool->shutdown && pool->q_count == pool->q_capacity) {
        /* wait for space */
        pthread_cond_wait(&pool->cond, &pool->mutex);
    }

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    /* enqueue */
    pool->queue[pool->q_tail].client_fd = client_fd;
    pool->queue[pool->q_tail].data = data;
    pool->queue[pool->q_tail].sems = sems;
    pool->q_tail = (pool->q_tail + 1) % pool->q_capacity;
    pool->q_count++;

    /* notify worker threads */
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

void destroy_thread_pool(thread_pool_t* pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool->threads);
    free(pool->queue);
    free(pool);
}
