#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <getopt.h>
#include <string.h>

#include "shared_mem.h"
#include "semaphores.h"
#include "master.h"
#include "worker.h"
#include <sys/wait.h>


// include config
#include "config.h"

volatile sig_atomic_t keep_running = 1;
pid_t *worker_pids = NULL;

static const char *VERSION_STR = "ConcurrentHTTP/1.0";

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("  -c, --config PATH     Configuration file (default: ./server.conf)\n");
    printf("  -p, --port PORT       Override listen port\n");
    printf("  -w, --workers NUM     Number of worker processes\n");
    printf("  -t, --threads NUM     Threads per worker\n");
    printf("  -d, --daemon          Run in background (daemonize)\n");
    printf("  -v, --verbose         Verbose startup logs\n");
    printf("  -h, --help            Show this help\n");
    printf("      --version         Show version\n");
}

static void daemonize_if_requested(int do_daemonize) {
    if (!do_daemonize) return;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork for daemon");
        exit(1);
    }
    if (pid > 0) {
        exit(0); // parent exits
    }

    if (setsid() < 0) {
        perror("setsid");
        exit(1);
    }

    pid = fork();
    if (pid < 0) {
        perror("fork for daemon stage2");
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }

    umask(0);
    chdir("/");

    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
}

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    int do_daemon = 0;
    int verbose = 0;
    int override_port = -1;
    int override_workers = -1;
    int override_threads = -1;
    const char *config_path = "server.conf";

    static struct option long_opts[] = {
        {"config",  required_argument, 0, 'c'},
        {"port",    required_argument, 0, 'p'},
        {"workers", required_argument, 0, 'w'},
        {"threads", required_argument, 0, 't'},
        {"daemon",  no_argument,       0, 'd'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0,  1 },
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "c:p:w:t:dvh", long_opts, &option_index)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'p': override_port = atoi(optarg); break;
            case 'w': override_workers = atoi(optarg); break;
            case 't': override_threads = atoi(optarg); break;
            case 'd': do_daemon = 1; break;
            case 'v': verbose = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            case 1:   printf("%s\n", VERSION_STR); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    if (verbose) {
        printf("[MAIN] Starting server initialization...\n");
    }

    // 1. Install signal handlers for graceful shutdown
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    // 2. Create shared memory
    shared_data_t* data = create_shared_memory();
    if (!data) {
        fprintf(stderr, "Failed to create shared memory.\n");
        exit(1);
    }
    if (verbose) printf("[MAIN] Shared memory created.\n");

    // 3. Load configuration
    server_config_t cfg;
    load_config(config_path, &cfg);

    if (override_port > 0) cfg.port = override_port;
    if (override_workers > 0) cfg.num_workers = override_workers;
    if (override_threads > 0) cfg.threads_per_worker = override_threads;

    if (verbose) {
        printf("[MAIN] Config: port=%d workers=%d threads=%d cache=%d bytes timeout=%d docroot=%s\n",
               cfg.port, cfg.num_workers, cfg.threads_per_worker,
               cfg.cache_max_bytes, cfg.timeout_seconds, cfg.document_root);
    }

    // 4. Initialize semaphores (respecting configured max queue size but capped)
    semaphores_t sems;
    int qsize = cfg.max_queue_size;
    if (qsize <= 0) qsize = MAX_QUEUE_SIZE;
    if (qsize > MAX_QUEUE_SIZE) qsize = MAX_QUEUE_SIZE;
    int nworkers = cfg.num_workers > 0 ? cfg.num_workers : 2;
    if (init_semaphores(&sems, nworkers, qsize) < 0) {
        fprintf(stderr, "Failed to initialize semaphores.\n");
        exit(1);
    }
    if (verbose) printf("[MAIN] Semaphores initialized.\n");

    // 5. Create unix socketpairs for FD passing, then fork worker processes
    if (verbose) printf("[MAIN] Forking %d workers...\n", nworkers);

    worker_pids = malloc(sizeof(pid_t) * nworkers);
    int (*sv)[2] = malloc(sizeof(int[2]) * nworkers);
    int *master_socks = malloc(sizeof(int) * nworkers);

    for (int i = 0; i < nworkers; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]) < 0) {
            perror("socketpair");
            exit(1);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            exit(1);
        }

        if (pid == 0) {
            // CHILD -> close master ends and keep worker end sv[i][1]
            for (int j = 0; j < nworkers; j++) {
                if (j == i) {
                    close(sv[j][0]);
                } else {
                    close(sv[j][0]); close(sv[j][1]);
                }
            }
            worker_comm_fd = sv[i][1];
            run_worker(data, &sems, i);
            exit(0);
        } else {
            // PARENT -> save worker PID and keep master end
            worker_pids[i] = pid;
            master_socks[i] = sv[i][0];
            close(sv[i][1]); // close worker end in parent
        }
    }

    // 6. Parent becomes MASTER
    if (verbose) printf("[MAIN] All workers forked. Entering master loop...\n");

    daemonize_if_requested(do_daemon);

    run_master(data, &sems, cfg.port, master_socks, nworkers);

    // Graceful shutdown: wait for all workers to exit
    if (verbose) printf("[MAIN] Waiting for workers to finish...\n");
    for (int i = 0; i < nworkers; i++) {
        int status;
        waitpid(worker_pids[i], &status, 0);
        if (verbose) printf("[MAIN] Worker %d (PID %d) exited.\n", i, worker_pids[i]);
    }

    if (verbose) printf("[MAIN] All workers terminated. Cleaning up...\n");
    destroy_semaphores(&sems);
    destroy_shared_memory(data);

    if (verbose) printf("[MAIN] Server shutdown complete.\n");
    return 0;
}
