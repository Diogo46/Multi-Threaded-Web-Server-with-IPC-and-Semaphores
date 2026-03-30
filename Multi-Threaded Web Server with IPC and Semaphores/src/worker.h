// worker.h
// Declarations for worker process logic and connection dequeue operations.


#ifndef WORKER_H
#define WORKER_H

#include "shared_mem.h"
#include "semaphores.h"
#include "thread_pool.h"

// Worker process main loop
void run_worker(shared_data_t* data, semaphores_t* sems, int worker_id);

// Dequeue a token from the per-worker shared queue
int dequeue_token(shared_data_t* data, semaphores_t* sems, int worker_id);

/* Per-worker unix-domain socket to receive passed FDs from master. */
extern int worker_comm_fd;


#endif
