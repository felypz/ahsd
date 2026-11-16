#include "router.h"

#include <string.h>

void router_init(Router *router) {
    if (router) {
        memset(router, 0, sizeof(*router));
    }
}

static size_t split_path(StrView path, StrView *out, size_t max_segments) {
    size_t count = 0;
    const char *p = path.ptr;
    const char *end = path.ptr + path.len;

    while (p < end) {
        while (p < end && *p == '/') {
            p++;
        }
        if (p >= end) {
            break;
        }
        const char *seg_start = p;
        while (p < end && *p != '/') {
            p++;
        }
        if (count >= max_segments) {
            return SIZE_MAX;
        }
        out[count++] = (StrView){seg_start, (size_t)(p - seg_start)};
    }

    return count;
}

bool router_add(Router *router, HttpMethod method, const char *pattern, RouteHandler handler,
                void *user_data) {
    if (!router || !pattern || !handler || router->route_count >= ROUTER_MAX_ROUTES) {
        return false;
    }

    StrView raw_segments[ROUTER_MAX_SEGMENTS];
    size_t count = split_path(str_view_from_cstr(pattern), raw_segments, ROUTER_MAX_SEGMENTS);
    if (count == SIZE_MAX) {
        return false;
    }

    Route *route = &router->routes[router->route_count];
    memset(route, 0, sizeof(*route));
    route->method = method;
    route->segment_count = count;
    route->handler = handler;
    route->user_data = user_data;

    for (size_t i = 0; i < count; i++) {
        StrView seg = raw_segments[i];
        if (seg.len > 1 && seg.ptr[0] == ':') {
            route->segments[i] = (RouteSegment){
                .kind = SEG_PARAM,
                .text = {seg.ptr + 1, seg.len - 1},
            };
        } else if (seg.len == 1 && seg.ptr[0] == '*') {
            route->segments[i] = (RouteSegment){
                .kind = SEG_WILDCARD,
                .text = seg,
            };
        } else {
            route->segments[i] = (RouteSegment){
                .kind = SEG_STATIC,
                .text = seg,
            };
        }
    }

    router->route_count++;
    return true;
}

static bool match_route(const Route *route, const StrView *path_segs, size_t path_count,
                        HttpRequest *req) {
    size_t param_idx = 0;

    for (size_t i = 0; i < route->segment_count; i++) {
        const RouteSegment *rseg = &route->segments[i];

        if (rseg->kind == SEG_WILDCARD) {
            return true;
        }
        if (i >= path_count) {
            return false;
        }

        StrView pseg = path_segs[i];
        if (rseg->kind == SEG_STATIC) {
            if (rseg->text.len != pseg.len || memcmp(rseg->text.ptr, pseg.ptr, pseg.len) != 0) {
                return false;
            }
        } else if (rseg->kind == SEG_PARAM) {
            if (param_idx >= HTTP_MAX_PARAMS) {
                return false;
            }
            req->params[param_idx++] = (HttpParam){
                .key = rseg->text,
                .value = pseg,
            };
        }
    }

    if (route->segment_count != path_count) {
        return false;
    }

    req->param_count = param_idx;
    return true;
}

void router_dispatch(const Router *router, HttpRequest *req, HttpResponse *res) {
    if (!router || !req || !res) {
        return;
    }

    StrView path_segs[ROUTER_MAX_SEGMENTS];
    size_t path_count = split_path(req->path, path_segs, ROUTER_MAX_SEGMENTS);
    if (path_count == SIZE_MAX) {
        http_res_json(res, 400, "{\"error\":\"path too deep\"}\n");
        return;
    }

    bool path_matched = false;

    for (size_t i = 0; i < router->route_count; i++) {
        const Route *route = &router->routes[i];
        req->param_count = 0;

        if (!match_route(route, path_segs, path_count, req)) {
            continue;
        }

        path_matched = true;
        if (route->method != req->method) {
            continue;
        }

        route->handler(req, res, route->user_data);
        return;
    }

    if (path_matched) {
        http_res_json(res, 405, "{\"error\":\"method not allowed\"}\n");
    } else {
        http_res_json(res, 404, "{\"error\":\"not found\"}\n");
    }
}
