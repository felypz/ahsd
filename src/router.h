#ifndef AHSD_ROUTER_H
#define AHSD_ROUTER_H

#include <stdbool.h>
#include <stddef.h>

#include "http.h"

#define ROUTER_MAX_ROUTES   128
#define ROUTER_MAX_SEGMENTS 16

typedef void (*RouteHandler)(const HttpRequest *req, HttpResponse *res, void *user_data);

typedef enum {
    SEG_STATIC = 0,
    SEG_PARAM,
    SEG_WILDCARD,
} SegmentKind;

typedef struct {
    SegmentKind kind;
    StrView text;
} RouteSegment;

typedef struct {
    HttpMethod method;
    RouteSegment segments[ROUTER_MAX_SEGMENTS];
    size_t segment_count;
    RouteHandler handler;
    void *user_data;
} Route;

typedef struct {
    Route routes[ROUTER_MAX_ROUTES];
    size_t route_count;
} Router;

void router_init(Router *router);
bool router_add(Router *router, HttpMethod method, const char *pattern, RouteHandler handler,
                void *user_data);
void router_dispatch(const Router *router, HttpRequest *req, HttpResponse *res);

#endif
