#include <signal.h>

#include "log.h"
#include "server.h"

static Server g_server;

static void on_signal(int sig) {
    (void)sig;
    server_stop(&g_server);
}

int main(void) {
    log_init(LOG_LEVEL_DEBUG);

    struct sigaction sa = {
        .sa_handler = on_signal,
    };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    ServerConfig config = {
        .port = 8080,
        .max_connections = 4096,
        .read_timeout_ms = 5000,
        .write_timeout_ms = 5000,
    };

    if (!server_init(&g_server, &config)) {
        LOG_ERROR("Failed to initialize server");
        return 1;
    }

    server_run(&g_server);

    LOG_INFO("Shutting down server");
    server_destroy(&g_server);
    return 0;
}
