#include <stdio.h>

#include "config.h"
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
    const StaticMount *mount = (const StaticMount *)user_data;

    server_route(srv, HTTP_METHOD_GET, "/health", handle_health, NULL);
    server_route(srv, HTTP_METHOD_GET, "/users/:id", handle_get_user, NULL);
    server_route(srv, HTTP_METHOD_POST, "/echo", handle_echo, NULL);
    server_mount_static(srv, mount);
}

int main(int argc, char **argv) {
    AppConfig app_cfg;
    ConfigParseResult parse_res = config_load(&app_cfg, argc, argv);
    if (parse_res == CONFIG_PARSE_EXIT_OK) {
        return 0;
    }
    if (parse_res == CONFIG_PARSE_ERROR) {
        return 1;
    }

    log_init(app_cfg.log_level);

    StaticMount static_mount = {
        .url_prefix = app_cfg.static_prefix,
        .root_dir = app_cfg.www_root,
    };

    MasterConfig master_cfg = {
        .server_config =
            {
                .port = app_cfg.port,
                .max_connections = app_cfg.max_connections,
                .read_timeout_ms = app_cfg.read_timeout_ms,
                .write_timeout_ms = app_cfg.write_timeout_ms,
            },
        .worker_count = app_cfg.worker_count,
        .setup_fn = setup_routes,
        .user_data = &static_mount,
    };

    if (!master_run(&master_cfg)) {
        LOG_ERROR("Failed to start master process");
        return 1;
    }

    return 0;
}
