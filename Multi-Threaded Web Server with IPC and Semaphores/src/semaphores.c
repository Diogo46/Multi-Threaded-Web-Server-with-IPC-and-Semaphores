// semaphores.c
#include "semaphores.h"
#include <fcntl.h>
#include <stdio.h>

int init_semaphores(semaphores_t* sems, int num_workers, int queue_size) {
    if (!sems) return -1;
    sems->num_workers = num_workers;
    char name[64];
    for (int i = 0; i < num_workers; i++) {
        snprintf(name, sizeof(name), "/ws_empty_%d", i);
        sems->empty_slots[i] = sem_open(name, O_CREAT, 0666, queue_size);
        snprintf(name, sizeof(name), "/ws_filled_%d", i);
        sems->filled_slots[i] = sem_open(name, O_CREAT, 0666, 0);
        snprintf(name, sizeof(name), "/ws_queue_mutex_%d", i);
        sems->queue_mutex[i] = sem_open(name, O_CREAT, 0666, 1);
        if (sems->empty_slots[i] == SEM_FAILED || sems->filled_slots[i] == SEM_FAILED || sems->queue_mutex[i] == SEM_FAILED) {
            return -1;
        }
    }

    sems->stats_mutex = sem_open("/ws_stats_mutex", O_CREAT, 0666, 1);
    sems->log_mutex = sem_open("/ws_log_mutex", O_CREAT, 0666, 1);

    if (sems->stats_mutex == SEM_FAILED || sems->log_mutex == SEM_FAILED) return -1;
    return 0;
}

void destroy_semaphores(semaphores_t* sems) {
    if (!sems) return;
    char name[64];
    for (int i = 0; i < sems->num_workers; i++) {
        sem_close(sems->empty_slots[i]);
        sem_close(sems->filled_slots[i]);
        sem_close(sems->queue_mutex[i]);
        snprintf(name, sizeof(name), "/ws_empty_%d", i); sem_unlink(name);
        snprintf(name, sizeof(name), "/ws_filled_%d", i); sem_unlink(name);
        snprintf(name, sizeof(name), "/ws_queue_mutex_%d", i); sem_unlink(name);
    }
    sem_close(sems->stats_mutex);
    sem_close(sems->log_mutex);
    sem_unlink("/ws_stats_mutex");
    sem_unlink("/ws_log_mutex");
}
