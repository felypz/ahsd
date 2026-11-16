#include "static_files.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *detect_mime_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcmp(dot, ".css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcmp(dot, ".js") == 0 || strcmp(dot, ".mjs") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (strcmp(dot, ".json") == 0) {
        return "application/json";
    }
    if (strcmp(dot, ".txt") == 0) {
        return "text/plain; charset=utf-8";
    }
    if (strcmp(dot, ".svg") == 0) {
        return "image/svg+xml";
    }
    if (strcmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(dot, ".webp") == 0) {
        return "image/webp";
    }
    if (strcmp(dot, ".ico") == 0) {
        return "image/x-icon";
    }
    if (strcmp(dot, ".wasm") == 0) {
        return "application/wasm";
    }
    return "application/octet-stream";
}

static bool has_path_traversal(StrView rel) {
    const char *p = rel.ptr;
    const char *end = rel.ptr + rel.len;

    while (p < end) {
        while (p < end && *p == '/') {
            p++;
        }
        const char *seg_start = p;
        while (p < end && *p != '/') {
            p++;
        }
        size_t seg_len = (size_t)(p - seg_start);
        if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.') {
            return true;
        }
    }
    return false;
}

static void handle_static_request(const HttpRequest *req, HttpResponse *res, void *user_data) {
    const StaticMount *mount = (const StaticMount *)user_data;
    size_t prefix_len = strlen(mount->url_prefix);

    StrView rel = {NULL, 0};
    if (req->path.len >= prefix_len) {
        rel.ptr = req->path.ptr + prefix_len;
        rel.len = req->path.len - prefix_len;
    }

    while (rel.len > 0 && rel.ptr[0] == '/') {
        rel.ptr++;
        rel.len--;
    }

    if (has_path_traversal(rel)) {
        http_res_json(res, 403, "{\"error\":\"forbidden\"}\n");
        return;
    }

    char full_path[PATH_MAX];
    int n;
    if (rel.len == 0) {
        n = snprintf(full_path, sizeof(full_path), "%s/index.html", mount->root_dir);
    } else {
        n = snprintf(full_path, sizeof(full_path), "%s/%.*s", mount->root_dir, (int)rel.len,
                     rel.ptr);
    }

    if (n < 0 || (size_t)n >= sizeof(full_path)) {
        http_res_json(res, 400, "{\"error\":\"invalid path\"}\n");
        return;
    }

    struct stat st;
    if (stat(full_path, &st) < 0) {
        if (errno == EACCES) {
            http_res_json(res, 403, "{\"error\":\"forbidden\"}\n");
        } else {
            http_res_json(res, 404, "{\"error\":\"not found\"}\n");
        }
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        size_t cur_len = (size_t)n;
        int idx_n = snprintf(full_path + cur_len, sizeof(full_path) - cur_len, "/index.html");
        if (idx_n < 0 || cur_len + (size_t)idx_n >= sizeof(full_path) ||
            stat(full_path, &st) < 0 || !S_ISREG(st.st_mode)) {
            http_res_json(res, 404, "{\"error\":\"not found\"}\n");
            return;
        }
    } else if (!S_ISREG(st.st_mode)) {
        http_res_json(res, 403, "{\"error\":\"forbidden\"}\n");
        return;
    }

    int fd = open(full_path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        http_res_json(res, 404, "{\"error\":\"not found\"}\n");
        return;
    }

    bool headers_only = (req->method == HTTP_METHOD_HEAD);
    if (!http_res_file(res, fd, (size_t)st.st_size, detect_mime_type(full_path), headers_only)) {
        http_res_json(res, 500, "{\"error\":\"internal error\"}\n");
    }
}

bool server_mount_static(Server *srv, const StaticMount *mount) {
    if (!srv || !mount || !mount->url_prefix || !mount->root_dir) {
        return false;
    }

    char pattern[256];
    size_t plen = strlen(mount->url_prefix);
    while (plen > 1 && mount->url_prefix[plen - 1] == '/') {
        plen--;
    }

    int n = snprintf(pattern, sizeof(pattern), "%.*s/*", (int)plen, mount->url_prefix);
    if (n < 0 || (size_t)n >= sizeof(pattern)) {
        return false;
    }

    if (!server_route(srv, HTTP_METHOD_GET, pattern, handle_static_request, (void *)mount)) {
        return false;
    }
    return server_route(srv, HTTP_METHOD_HEAD, pattern, handle_static_request, (void *)mount);
}
