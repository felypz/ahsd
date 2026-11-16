#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

StrView str_view_from_cstr(const char *s) {
    if (!s) {
        return (StrView){NULL, 0};
    }
    return (StrView){s, strlen(s)};
}

bool str_view_eq(StrView a, const char *b) {
    if (!b) {
        return false;
    }
    size_t b_len = strlen(b);
    return a.len == b_len && memcmp(a.ptr, b, b_len) == 0;
}

bool str_view_ieq(StrView a, const char *b) {
    if (!b) {
        return false;
    }
    size_t b_len = strlen(b);
    if (a.len != b_len) {
        return false;
    }
    for (size_t i = 0; i < b_len; i++) {
        if (tolower((unsigned char)a.ptr[i]) != tolower((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

const char *http_method_name(HttpMethod method) {
    switch (method) {
        case HTTP_METHOD_GET:
            return "GET";
        case HTTP_METHOD_POST:
            return "POST";
        case HTTP_METHOD_PUT:
            return "PUT";
        case HTTP_METHOD_DELETE:
            return "DELETE";
        case HTTP_METHOD_PATCH:
            return "PATCH";
        case HTTP_METHOD_HEAD:
            return "HEAD";
        case HTTP_METHOD_OPTIONS:
            return "OPTIONS";
        default:
            return "UNKNOWN";
    }
}

static HttpMethod parse_method(StrView s) {
    if (str_view_eq(s, "GET")) {
        return HTTP_METHOD_GET;
    }
    if (str_view_eq(s, "POST")) {
        return HTTP_METHOD_POST;
    }
    if (str_view_eq(s, "PUT")) {
        return HTTP_METHOD_PUT;
    }
    if (str_view_eq(s, "DELETE")) {
        return HTTP_METHOD_DELETE;
    }
    if (str_view_eq(s, "PATCH")) {
        return HTTP_METHOD_PATCH;
    }
    if (str_view_eq(s, "HEAD")) {
        return HTTP_METHOD_HEAD;
    }
    if (str_view_eq(s, "OPTIONS")) {
        return HTTP_METHOD_OPTIONS;
    }
    return HTTP_METHOD_UNKNOWN;
}

static const char *find_crlf(const char *start, const char *end) {
    for (const char *p = start; p + 1 < end; p++) {
        if (p[0] == '\r' && p[1] == '\n') {
            return p;
        }
    }
    return NULL;
}

static bool parse_content_length(StrView val, size_t *out) {
    if (val.len == 0) {
        return false;
    }
    size_t acc = 0;
    for (size_t i = 0; i < val.len; i++) {
        unsigned char c = (unsigned char)val.ptr[i];
        if (!isdigit(c)) {
            return false;
        }
        size_t digit = (size_t)(c - '0');
        if (acc > (SIZE_MAX - digit) / 10) {
            return false;
        }
        acc = (acc * 10) + digit;
    }
    *out = acc;
    return true;
}

static void parse_query_params(HttpRequest *req) {
    if (!req->query.ptr || req->query.len == 0) {
        return;
    }

    const char *p = req->query.ptr;
    const char *end = req->query.ptr + req->query.len;

    while (p < end && req->query_param_count < HTTP_MAX_QUERY_PARAMS) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        const char *pair_end = amp ? amp : end;

        if (pair_end > p) {
            const char *eq = memchr(p, '=', (size_t)(pair_end - p));
            if (eq) {
                req->query_params[req->query_param_count++] = (HttpParam){
                    .key = {p, (size_t)(eq - p)},
                    .value = {eq + 1, (size_t)(pair_end - (eq + 1))},
                };
            } else {
                req->query_params[req->query_param_count++] = (HttpParam){
                    .key = {p, (size_t)(pair_end - p)},
                    .value = {NULL, 0},
                };
            }
        }

        if (!amp) {
            break;
        }
        p = amp + 1;
    }
}

StrView http_req_header(const HttpRequest *req, const char *name) {
    if (!req || !name) {
        return (StrView){NULL, 0};
    }
    for (size_t i = 0; i < req->header_count; i++) {
        if (str_view_ieq(req->headers[i].name, name)) {
            return req->headers[i].value;
        }
    }
    return (StrView){NULL, 0};
}

StrView http_req_param(const HttpRequest *req, const char *key) {
    if (!req || !key) {
        return (StrView){NULL, 0};
    }
    for (size_t i = 0; i < req->param_count; i++) {
        if (str_view_eq(req->params[i].key, key)) {
            return req->params[i].value;
        }
    }
    return (StrView){NULL, 0};
}

StrView http_req_query(const HttpRequest *req, const char *key) {
    if (!req || !key) {
        return (StrView){NULL, 0};
    }
    for (size_t i = 0; i < req->query_param_count; i++) {
        if (str_view_eq(req->query_params[i].key, key)) {
            return req->query_params[i].value;
        }
    }
    return (StrView){NULL, 0};
}

HttpParseStatus http_parse_request(const char *buf, size_t len, HttpRequest *req) {
    if (!buf || !req) {
        return HTTP_PARSE_ERROR;
    }

    memset(req, 0, sizeof(*req));

    const char *end = buf + len;
    const char *line_end = find_crlf(buf, end);
    if (!line_end) {
        return HTTP_PARSE_INCOMPLETE;
    }

    const char *sp1 = memchr(buf, ' ', (size_t)(line_end - buf));
    if (!sp1 || sp1 == buf) {
        return HTTP_PARSE_ERROR;
    }

    req->method_str = (StrView){buf, (size_t)(sp1 - buf)};
    req->method = parse_method(req->method_str);
    if (req->method == HTTP_METHOD_UNKNOWN) {
        return HTTP_PARSE_ERROR;
    }

    const char *target_start = sp1 + 1;
    const char *sp2 = memchr(target_start, ' ', (size_t)(line_end - target_start));
    if (!sp2 || sp2 == target_start) {
        return HTTP_PARSE_ERROR;
    }

    req->target = (StrView){target_start, (size_t)(sp2 - target_start)};
    const char *qmark = memchr(req->target.ptr, '?', req->target.len);
    if (qmark) {
        req->path = (StrView){req->target.ptr, (size_t)(qmark - req->target.ptr)};
        req->query =
            (StrView){qmark + 1, req->target.len - (size_t)(qmark - req->target.ptr) - 1};
        parse_query_params(req);
    } else {
        req->path = req->target;
        req->query = (StrView){NULL, 0};
    }

    const char *ver_start = sp2 + 1;
    size_t ver_len = (size_t)(line_end - ver_start);
    if (ver_len != 8 || memcmp(ver_start, "HTTP/1.", 7) != 0) {
        return HTTP_PARSE_ERROR;
    }
    if (ver_start[7] == '1') {
        req->minor_version = 1;
    } else if (ver_start[7] == '0') {
        req->minor_version = 0;
    } else {
        return HTTP_PARSE_ERROR;
    }

    const char *cursor = line_end + 2;
    for (;;) {
        const char *hdr_end = find_crlf(cursor, end);
        if (!hdr_end) {
            return HTTP_PARSE_INCOMPLETE;
        }
        if (hdr_end == cursor) {
            cursor = hdr_end + 2;
            break;
        }

        if (req->header_count >= HTTP_MAX_HEADERS) {
            return HTTP_PARSE_ERROR;
        }

        const char *colon = memchr(cursor, ':', (size_t)(hdr_end - cursor));
        if (!colon || colon == cursor) {
            return HTTP_PARSE_ERROR;
        }

        const char *val_start = colon + 1;
        while (val_start < hdr_end && (*val_start == ' ' || *val_start == '\t')) {
            val_start++;
        }
        const char *val_end = hdr_end;
        while (val_end > val_start && (val_end[-1] == ' ' || val_end[-1] == '\t')) {
            val_end--;
        }

        req->headers[req->header_count++] = (HttpHeader){
            .name = {cursor, (size_t)(colon - cursor)},
            .value = {val_start, (size_t)(val_end - val_start)},
        };

        cursor = hdr_end + 2;
    }

    size_t headers_total = (size_t)(cursor - buf);
    size_t content_len = 0;
    StrView cl_hdr = http_req_header(req, "Content-Length");
    if (cl_hdr.ptr != NULL) {
        if (!parse_content_length(cl_hdr, &content_len)) {
            return HTTP_PARSE_ERROR;
        }
    }

    if (len < headers_total + content_len) {
        return HTTP_PARSE_INCOMPLETE;
    }

    req->body = (StrView){cursor, content_len};
    req->raw_len = headers_total + content_len;

    StrView conn_hdr = http_req_header(req, "Connection");
    if (req->minor_version == 1) {
        req->keep_alive = !str_view_ieq(conn_hdr, "close");
    } else {
        req->keep_alive = str_view_ieq(conn_hdr, "keep-alive");
    }

    return HTTP_PARSE_OK;
}

static const char *status_reason(int code) {
    switch (code) {
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 413:
            return "Payload Too Large";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

static const char *cached_http_date(void) {
    static time_t last_sec = 0;
    static char date_buf[32] = "Thu, 01 Jan 1970 00:00:00 GMT";

    time_t now = time(NULL);
    if (now != last_sec) {
        struct tm gmt;
        gmtime_r(&now, &gmt);
        strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);
        last_sec = now;
    }
    return date_buf;
}

void http_res_init(HttpResponse *res, char *buf, size_t cap, bool keep_alive) {
    res->status_code = 200;
    res->buf = buf;
    res->cap = cap;
    res->len = 0;
    res->headers_written = false;
    res->keep_alive = keep_alive;
    res->file_fd = -1;
    res->file_size = 0;
}

void http_res_status(HttpResponse *res, int status_code) {
    res->status_code = status_code;
}

static bool res_ensure_status_line(HttpResponse *res) {
    if (res->headers_written) {
        return true;
    }
    int n = snprintf(res->buf, res->cap,
                     "HTTP/1.1 %d %s\r\n"
                     "Server: ahsd\r\n"
                     "Date: %s\r\n",
                     res->status_code, status_reason(res->status_code), cached_http_date());
    if (n < 0 || (size_t)n >= res->cap) {
        return false;
    }
    res->len = (size_t)n;
    res->headers_written = true;
    return true;
}

bool http_res_header(HttpResponse *res, const char *name, const char *value) {
    if (!res_ensure_status_line(res)) {
        return false;
    }
    int n = snprintf(res->buf + res->len, res->cap - res->len, "%s: %s\r\n", name, value);
    if (n < 0 || res->len + (size_t)n >= res->cap) {
        return false;
    }
    res->len += (size_t)n;
    return true;
}

bool http_res_body(HttpResponse *res, const char *content_type, const void *data, size_t len) {
    if (!res_ensure_status_line(res)) {
        return false;
    }

    if (content_type && !http_res_header(res, "Content-Type", content_type)) {
        return false;
    }

    int n = snprintf(res->buf + res->len, res->cap - res->len,
                     "Content-Length: %zu\r\n"
                     "Connection: %s\r\n"
                     "\r\n",
                     len, res->keep_alive ? "keep-alive" : "close");
    if (n < 0 || res->len + (size_t)n >= res->cap) {
        return false;
    }
    res->len += (size_t)n;

    if (res->len + len > res->cap) {
        return false;
    }
    if (len > 0 && data) {
        memcpy(res->buf + res->len, data, len);
        res->len += len;
    }
    return true;
}

bool http_res_file(HttpResponse *res, int file_fd, size_t file_size, const char *content_type,
                   bool headers_only) {
    if (file_fd < 0 || !res_ensure_status_line(res)) {
        if (file_fd >= 0) {
            close(file_fd);
        }
        return false;
    }

    if (content_type && !http_res_header(res, "Content-Type", content_type)) {
        close(file_fd);
        return false;
    }

    int n = snprintf(res->buf + res->len, res->cap - res->len,
                     "Content-Length: %zu\r\n"
                     "Connection: %s\r\n"
                     "\r\n",
                     file_size, res->keep_alive ? "keep-alive" : "close");
    if (n < 0 || res->len + (size_t)n >= res->cap) {
        close(file_fd);
        return false;
    }
    res->len += (size_t)n;

    if (headers_only || file_size == 0) {
        close(file_fd);
        res->file_fd = -1;
        res->file_size = 0;
    } else {
        res->file_fd = file_fd;
        res->file_size = file_size;
    }
    return true;
}

bool http_res_text(HttpResponse *res, int status_code, const char *text) {
    http_res_status(res, status_code);
    size_t len = text ? strlen(text) : 0;
    return http_res_body(res, "text/plain; charset=utf-8", text, len);
}

bool http_res_json(HttpResponse *res, int status_code, const char *json) {
    http_res_status(res, status_code);
    size_t len = json ? strlen(json) : 0;
    return http_res_body(res, "application/json", json, len);
}
