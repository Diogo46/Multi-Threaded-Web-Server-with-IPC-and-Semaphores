#ifndef MASTER_H
#define MASTER_H

#include <signal.h> // required for sig_atomic_t
#include "shared_mem.h"
#include "semaphores.h"


extern volatile sig_atomic_t keep_running;

/*
 * Initializes the server socket.
 * Returns the socket file descriptor on success, -1 on error.
 */
int create_server_socket(int port);

/*
 * Enqueue a new client connection into the shared memory queue.
 * Called by the master process after accept().
 */
void enqueue_connection(shared_data_t* data, semaphores_t* sems, int client_fd, int worker_id);

/*
 * Main master-process loop. Accepts connections and distributes them.
 * `port` is the TCP port to bind and listen on.
 */
void run_master(shared_data_t* data, semaphores_t* sems, int port, int* master_socks, int num_workers);

#endif
