#include <signal.h>
#include <stdio.h>

#include "log.h"
#include "server.h"

static Server g_server;

static void on_signal(int sig) {
    (void)sig;
    server_stop(&g_server);
}

static void handle_health(const HttpRequest *req, HttpResponse *res, void *user_data) {
    (void)req;
    (void)user_data;
    http_res_json(res, 200, "{\"status\":\"ok\",\"server\":\"ahsd\"}\n");
}

static void handle_get_user(const HttpRequest *req, HttpResponse *res, void *user_data) {
    (void)user_data;
    StrView id = http_req_param(req, "id");

    char body[256];
    int n = snprintf(body, sizeof(body), "{\"user_id\":\"%.*s\"}\n", (int)id.len, id.ptr);
    if (n < 0 || (size_t)n >= sizeof(body)) {
        http_res_json(res, 500, "{\"error\":\"internal error\"}\n");
        return;
    }

    http_res_json(res, 200, body);
}

static void handle_echo(const HttpRequest *req, HttpResponse *res, void *user_data) {
    (void)user_data;
    http_res_status(res, 200);
    http_res_body(res, "application/octet-stream", req->body.ptr, req->body.len);
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

    server_route(&g_server, HTTP_METHOD_GET, "/health", handle_health, NULL);
    server_route(&g_server, HTTP_METHOD_GET, "/users/:id", handle_get_user, NULL);
    server_route(&g_server, HTTP_METHOD_POST, "/echo", handle_echo, NULL);

    server_run(&g_server);

    LOG_INFO("Shutting down server");
    server_destroy(&g_server);
    return 0;
}
