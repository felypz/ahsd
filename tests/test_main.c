#include <string.h>
#include <unistd.h>

#include "http.h"
#include "mem_pool.h"
#include "router.h"
#include "test_runner.h"
#include "timer_heap.h"

static void test_mem_pool_lifecycle(void) {
    MemPool pool;
    ASSERT_TRUE(mem_pool_init(&pool, 32, 4));
    ASSERT_EQ_SIZE(4, mem_pool_available(&pool));

    void *a = mem_pool_alloc(&pool);
    void *b = mem_pool_alloc(&pool);
    void *c = mem_pool_alloc(&pool);
    void *d = mem_pool_alloc(&pool);
    ASSERT_TRUE(a && b && c && d);
    ASSERT_EQ_SIZE(0, mem_pool_available(&pool));
    ASSERT_TRUE(mem_pool_alloc(&pool) == NULL);

    mem_pool_free(&pool, b);
    ASSERT_EQ_SIZE(1, mem_pool_available(&pool));

    void *reused = mem_pool_alloc(&pool);
    ASSERT_TRUE(reused == b);

    mem_pool_reset(&pool);
    ASSERT_EQ_SIZE(4, mem_pool_available(&pool));

    mem_pool_destroy(&pool);
}

static int g_timer_order[4];
static size_t g_timer_hits = 0;

static void record_timer_cb(void *user_data) {
    int id = *(int *)user_data;
    if (g_timer_hits < 4) {
        g_timer_order[g_timer_hits++] = id;
    }
}

static void test_timer_heap_ordering_and_upsert(void) {
    TimerHeap heap;
    ASSERT_TRUE(timer_heap_init(&heap, 8));
    ASSERT_EQ_INT(-1, timer_heap_next_timeout_ms(&heap));

    int ids[3] = {1, 2, 3};
    TimerNode nodes[3];
    for (int i = 0; i < 3; i++) {
        timer_node_init(&nodes[i], record_timer_cb, &ids[i]);
    }

    g_timer_hits = 0;
    ASSERT_TRUE(timer_heap_upsert(&heap, &nodes[0], 40));
    ASSERT_TRUE(timer_heap_upsert(&heap, &nodes[1], 10));
    ASSERT_TRUE(timer_heap_upsert(&heap, &nodes[2], 25));

    ASSERT_TRUE(timer_heap_upsert(&heap, &nodes[0], 5));

    usleep(50000);
    size_t fired = timer_heap_process_expired(&heap);
    ASSERT_EQ_SIZE(3, fired);
    ASSERT_EQ_SIZE(3, g_timer_hits);
    ASSERT_EQ_INT(1, g_timer_order[0]);
    ASSERT_EQ_INT(2, g_timer_order[1]);
    ASSERT_EQ_INT(3, g_timer_order[2]);

    timer_heap_destroy(&heap);
}

static void test_http_parse_complete_and_query(void) {
    const char raw[] =
        "POST /users/99?role=admin&active=1 HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "{\"ok\":true}\r\n";

    HttpRequest req;
    HttpParseStatus st = http_parse_request(raw, sizeof(raw) - 1, &req);
    ASSERT_EQ_INT(HTTP_PARSE_OK, st);
    ASSERT_EQ_INT(HTTP_METHOD_POST, req.method);
    ASSERT_TRUE(str_view_eq(req.path, "/users/99"));
    ASSERT_TRUE(str_view_eq(http_req_query(&req, "role"), "admin"));
    ASSERT_TRUE(str_view_eq(http_req_query(&req, "active"), "1"));
    ASSERT_TRUE(str_view_eq(http_req_header(&req, "content-type"), "application/json"));
    ASSERT_TRUE(str_view_eq(req.body, "{\"ok\":true}\r\n"));
    ASSERT_TRUE(req.keep_alive);
}

static void test_http_parse_partial_and_invalid(void) {
    const char partial[] = "GET /health HTTP/1.1\r\nHost: local";
    HttpRequest req;
    ASSERT_EQ_INT(HTTP_PARSE_INCOMPLETE, http_parse_request(partial, sizeof(partial) - 1, &req));

    const char invalid[] = "FOOBAR / HTTP/1.1\r\n\r\n";
    ASSERT_EQ_INT(HTTP_PARSE_ERROR, http_parse_request(invalid, sizeof(invalid) - 1, &req));
}

static void dummy_user_handler(const HttpRequest *req, HttpResponse *res, void *user_data) {
    (void)user_data;
    StrView id = http_req_param(req, "id");
    if (str_view_eq(id, "42")) {
        http_res_json(res, 200, "{\"found\":true}");
    } else {
        http_res_json(res, 200, "{\"found\":false}");
    }
}

static void test_router_matching(void) {
    Router router;
    router_init(&router);
    ASSERT_TRUE(router_add(&router, HTTP_METHOD_GET, "/users/:id", dummy_user_handler, NULL));

    char out[1024];

    const char req_ok[] = "GET /users/42 HTTP/1.1\r\nHost: x\r\n\r\n";
    HttpRequest req;
    HttpResponse res;
    ASSERT_EQ_INT(HTTP_PARSE_OK, http_parse_request(req_ok, sizeof(req_ok) - 1, &req));
    http_res_init(&res, out, sizeof(out), false);
    router_dispatch(&router, &req, &res);
    ASSERT_EQ_INT(200, res.status_code);
    ASSERT_TRUE(strstr(out, "{\"found\":true}") != NULL);

    const char req_405[] = "DELETE /users/42 HTTP/1.1\r\nHost: x\r\n\r\n";
    ASSERT_EQ_INT(HTTP_PARSE_OK, http_parse_request(req_405, sizeof(req_405) - 1, &req));
    http_res_init(&res, out, sizeof(out), false);
    router_dispatch(&router, &req, &res);
    ASSERT_EQ_INT(405, res.status_code);

    const char req_404[] = "GET /missing HTTP/1.1\r\nHost: x\r\n\r\n";
    ASSERT_EQ_INT(HTTP_PARSE_OK, http_parse_request(req_404, sizeof(req_404) - 1, &req));
    http_res_init(&res, out, sizeof(out), false);
    router_dispatch(&router, &req, &res);
    ASSERT_EQ_INT(404, res.status_code);
}

int main(void) {
    printf("Running ahsd unit tests...\n");
    RUN_TEST(test_mem_pool_lifecycle);
    RUN_TEST(test_timer_heap_ordering_and_upsert);
    RUN_TEST(test_http_parse_complete_and_query);
    RUN_TEST(test_http_parse_partial_and_invalid);
    RUN_TEST(test_router_matching);

    printf("Summary: %d run, %d failed\n", g_tests_run, g_tests_failed);
    return g_tests_failed == 0 ? 0 : 1;
}
