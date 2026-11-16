#include "worker.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "log.h"

static volatile sig_atomic_t g_master_running = 1;
static Server *g_worker_server = NULL;

static void on_master_signal(int sig) {
    (void)sig;
    g_master_running = 0;
}

static void on_worker_signal(int sig) {
    (void)sig;
    if (g_worker_server) {
        server_stop(g_worker_server);
    }
}

static size_t resolve_worker_count(size_t configured) {
    if (configured > 0) {
        return configured;
    }
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    return nprocs > 0 ? (size_t)nprocs : 1;
}

static void run_worker_process(const MasterConfig *config) {
    Server srv;
    g_worker_server = &srv;

    struct sigaction sa = {
        .sa_handler = on_worker_signal,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    if (!server_init(&srv, &config->server_config)) {
        LOG_ERROR("Worker failed to initialize server");
        _exit(1);
    }

    if (config->setup_fn) {
        config->setup_fn(&srv, config->user_data);
    }

    server_run(&srv);

    LOG_INFO("Worker shutting down");
    server_destroy(&srv);
    _exit(0);
}

static pid_t spawn_worker(const MasterConfig *config) {
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("fork() failed: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        run_worker_process(config);
    }
    LOG_INFO("Spawned worker pid=%d", (int)pid);
    return pid;
}

bool master_run(const MasterConfig *config) {
    if (!config) {
        return false;
    }

    size_t count = resolve_worker_count(config->worker_count);
    pid_t *workers = calloc(count, sizeof(pid_t));
    if (!workers) {
        return false;
    }

    struct sigaction sa = {
        .sa_handler = on_master_signal,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    LOG_INFO("Master starting with %zu worker(s) on port %u", count, config->server_config.port);

    for (size_t i = 0; i < count; i++) {
        workers[i] = spawn_worker(config);
        if (workers[i] < 0) {
            g_master_running = 0;
            break;
        }
    }

    while (g_master_running) {
        int status = 0;
        pid_t dead_pid = waitpid(-1, &status, 0);
        if (dead_pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (!g_master_running) {
            break;
        }

        LOG_WARN("Worker pid=%d exited unexpectedly (status=%d), respawning", (int)dead_pid,
                 status);

        for (size_t i = 0; i < count; i++) {
            if (workers[i] == dead_pid) {
                workers[i] = spawn_worker(config);
                break;
            }
        }
    }

    LOG_INFO("Master shutting down workers");
    for (size_t i = 0; i < count; i++) {
        if (workers[i] > 0) {
            kill(workers[i], SIGTERM);
        }
    }

    for (size_t i = 0; i < count; i++) {
        if (workers[i] > 0) {
            waitpid(workers[i], NULL, 0);
        }
    }

    free(workers);
    LOG_INFO("Master shutdown complete");
    return true;
}
