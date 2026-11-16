#ifndef AHSD_HTTP_H
#define AHSD_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTTP_MAX_HEADERS      32
#define HTTP_MAX_PARAMS       16
#define HTTP_MAX_QUERY_PARAMS 32

typedef struct {
    const char *ptr;
    size_t len;
} StrView;

typedef enum {
    HTTP_METHOD_UNKNOWN = 0,
    HTTP_METHOD_GET,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
} HttpMethod;

typedef enum {
    HTTP_PARSE_OK = 0,
    HTTP_PARSE_INCOMPLETE,
    HTTP_PARSE_ERROR,
} HttpParseStatus;

typedef struct {
    StrView name;
    StrView value;
} HttpHeader;

typedef struct {
    StrView key;
    StrView value;
} HttpParam;

typedef struct {
    HttpMethod method;
    StrView method_str;
    StrView target;
    StrView path;
    StrView query;
    int minor_version;

    HttpHeader headers[HTTP_MAX_HEADERS];
    size_t header_count;

    HttpParam params[HTTP_MAX_PARAMS];
    size_t param_count;

    HttpParam query_params[HTTP_MAX_QUERY_PARAMS];
    size_t query_param_count;

    StrView body;
    size_t raw_len;
    bool keep_alive;
} HttpRequest;

typedef struct {
    int status_code;
    char *buf;
    size_t cap;
    size_t len;
    bool headers_written;
    bool keep_alive;

    int file_fd;
    size_t file_size;
} HttpResponse;

StrView str_view_from_cstr(const char *s);
bool str_view_eq(StrView a, const char *b);
bool str_view_ieq(StrView a, const char *b);

const char *http_method_name(HttpMethod method);
HttpParseStatus http_parse_request(const char *buf, size_t len, HttpRequest *req);

StrView http_req_header(const HttpRequest *req, const char *name);
StrView http_req_param(const HttpRequest *req, const char *key);
StrView http_req_query(const HttpRequest *req, const char *key);

void http_res_init(HttpResponse *res, char *buf, size_t cap, bool keep_alive);
void http_res_status(HttpResponse *res, int status_code);
bool http_res_header(HttpResponse *res, const char *name, const char *value);
bool http_res_body(HttpResponse *res, const char *content_type, const void *data, size_t len);
bool http_res_file(HttpResponse *res, int file_fd, size_t file_size, const char *content_type,
                   bool headers_only);
bool http_res_text(HttpResponse *res, int status_code, const char *text);
bool http_res_json(HttpResponse *res, int status_code, const char *json);

#endif
