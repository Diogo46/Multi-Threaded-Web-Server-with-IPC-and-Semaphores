// semaphores.h
#ifndef SEMAPHORES_H
#define SEMAPHORES_H

#include <semaphore.h>

#include "shared_mem.h"

typedef struct {
    sem_t* empty_slots[MAX_WORKERS];
    sem_t* filled_slots[MAX_WORKERS];
    sem_t* queue_mutex[MAX_WORKERS];
    sem_t* stats_mutex;
    sem_t* log_mutex;
    int num_workers;
} semaphores_t;

int init_semaphores(semaphores_t* sems, int num_workers, int queue_size);
void destroy_semaphores(semaphores_t* sems);

#endif
