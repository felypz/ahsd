#ifndef AHSD_WORKER_H
#define AHSD_WORKER_H

#include <stdbool.h>
#include <stddef.h>

#include "server.h"

typedef void (*WorkerSetupFn)(Server *srv, void *user_data);

typedef struct {
    ServerConfig server_config;
    size_t worker_count;
    WorkerSetupFn setup_fn;
    void *user_data;
} MasterConfig;

bool master_run(const MasterConfig *config);

#endif
