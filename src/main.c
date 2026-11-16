#include <stdio.h>

#include "log.h"
#include "static_files.h"
#include "worker.h"

static void handle_health(const HttpRequest *req, HttpResponse *res, void *user_data) {
    (void)req;
    (void)user_data;
    http_res_json(res, 200, "{\"status\":\"ok\",\"server\":\"ahsd\"}\n");
}

static void handle_get_user(const HttpRequest *req, HttpResponse *res, void *user_data) {
    (void)user_data;
    StrView id = http_req_param(req, "id");
    StrView role = http_req_query(req, "role");

    char body[256];
    int n = snprintf(body, sizeof(body), "{\"user_id\":\"%.*s\",\"role\":\"%.*s\"}\n",
                     (int)id.len, id.ptr, (int)role.len, role.ptr);
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

static void setup_routes(Server *srv, void *user_data) {
    (void)user_data;

    static const StaticMount public_mount = {
        .url_prefix = "/static",
        .root_dir = "./public",
    };

    server_route(srv, HTTP_METHOD_GET, "/health", handle_health, NULL);
    server_route(srv, HTTP_METHOD_GET, "/users/:id", handle_get_user, NULL);
    server_route(srv, HTTP_METHOD_POST, "/echo", handle_echo, NULL);
    server_mount_static(srv, &public_mount);
}

int main(void) {
    log_init(LOG_LEVEL_INFO);

    MasterConfig config = {
        .server_config =
            {
                .port = 8080,
                .max_connections = 4096,
                .read_timeout_ms = 5000,
                .write_timeout_ms = 5000,
            },
        .worker_count = 0,
        .setup_fn = setup_routes,
        .user_data = NULL,
    };

    if (!master_run(&config)) {
        LOG_ERROR("Failed to start master process");
        return 1;
    }

    return 0;
}
