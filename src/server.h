#ifndef AHSD_SERVER_H
#define AHSD_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mem_pool.h"
#include "timer_heap.h"

#define AHSD_READ_BUF_SIZE  4096
#define AHSD_WRITE_BUF_SIZE 4096

typedef enum {
    CONN_STATE_READING = 0,
    CONN_STATE_WRITING,
    CONN_STATE_CLOSING,
} ConnState;

typedef struct Server Server;

typedef struct {
    int fd;
    ConnState state;
    Server *server;
    TimerNode timer;

    char read_buf[AHSD_READ_BUF_SIZE];
    size_t read_len;

    char write_buf[AHSD_WRITE_BUF_SIZE];
    size_t write_len;
    size_t write_offset;
} Connection;

typedef struct {
    uint16_t port;
    size_t max_connections;
    uint64_t read_timeout_ms;
    uint64_t write_timeout_ms;
} ServerConfig;

struct Server {
    ServerConfig config;
    int listen_fd;
    int epoll_fd;
    bool running;

    MemPool conn_pool;
    TimerHeap timer_heap;
};

bool server_init(Server *srv, const ServerConfig *config);
void server_run(Server *srv);
void server_stop(Server *srv);
void server_destroy(Server *srv);

#endif
