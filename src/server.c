#include "server.h"

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log.h"

#define MAX_EPOLL_EVENTS 128

static void conn_close(Connection *conn);

static void on_conn_timeout(void *user_data) {
    Connection *conn = (Connection *)user_data;
    LOG_DEBUG("Connection timeout fd=%d state=%d", conn->fd, conn->state);
    conn_close(conn);
}

static bool epoll_set_events(int epoll_fd, int fd, uint32_t events, void *ptr) {
    struct epoll_event ev = {
        .events = events,
        .data.ptr = ptr,
    };
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

static void conn_close(Connection *conn) {
    if (!conn || conn->fd < 0) {
        return;
    }

    Server *srv = conn->server;
    int fd = conn->fd;
    conn->fd = -1;
    conn->state = CONN_STATE_CLOSING;

    timer_heap_remove(&srv->timer_heap, &conn->timer);
    epoll_ctl(srv->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);

    mem_pool_free(&srv->conn_pool, conn);
    LOG_DEBUG("Connection closed fd=%d available_slots=%zu", fd,
              mem_pool_available(&srv->conn_pool));
}

static int listen_socket_create(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        LOG_ERROR("socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(port),
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind() failed on port %u: %s", port, strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, SOMAXCONN) < 0) {
        LOG_ERROR("listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

static void accept_pending(Server *srv) {
    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept4(srv->listen_fd, (struct sockaddr *)&client_addr, &addr_len,
                                SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            LOG_WARN("accept4() failed: %s", strerror(errno));
            break;
        }

        Connection *conn = mem_pool_alloc(&srv->conn_pool);
        if (!conn) {
            LOG_WARN("Connection pool exhausted, rejecting fd=%d", client_fd);
            close(client_fd);
            continue;
        }

        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        conn->fd = client_fd;
        conn->state = CONN_STATE_READING;
        conn->server = srv;
        timer_node_init(&conn->timer, on_conn_timeout, conn);

        struct epoll_event ev = {
            .events = EPOLLIN | EPOLLET | EPOLLRDHUP,
            .data.ptr = conn,
        };

        if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            LOG_ERROR("epoll_ctl(ADD) failed for fd=%d: %s", client_fd, strerror(errno));
            close(client_fd);
            mem_pool_free(&srv->conn_pool, conn);
            continue;
        }

        timer_heap_upsert(&srv->timer_heap, &conn->timer, srv->config.read_timeout_ms);
        LOG_DEBUG("Connection accepted fd=%d", client_fd);
    }
}

static void build_default_response(Connection *conn) {
    static const char body[] = "{\"status\":\"ok\",\"server\":\"ahsd\"}\n";
    int n = snprintf(conn->write_buf, sizeof(conn->write_buf),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     sizeof(body) - 1, body);

    if (n < 0 || (size_t)n >= sizeof(conn->write_buf)) {
        conn_close(conn);
        return;
    }

    conn->write_len = (size_t)n;
    conn->write_offset = 0;
    conn->state = CONN_STATE_WRITING;

    timer_heap_upsert(&conn->server->timer_heap, &conn->timer,
                      conn->server->config.write_timeout_ms);

    if (!epoll_set_events(conn->server->epoll_fd, conn->fd, EPOLLOUT | EPOLLET | EPOLLRDHUP,
                          conn)) {
        conn_close(conn);
    }
}

static void conn_handle_write(Connection *conn) {
    while (conn->write_offset < conn->write_len) {
        ssize_t n = send(conn->fd, conn->write_buf + conn->write_offset,
                         conn->write_len - conn->write_offset, MSG_NOSIGNAL);
        if (n > 0) {
            conn->write_offset += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        conn_close(conn);
        return;
    }

    conn_close(conn);
}

static void conn_handle_read(Connection *conn) {
    for (;;) {
        size_t space = sizeof(conn->read_buf) - conn->read_len - 1;
        if (space == 0) {
            LOG_WARN("Read buffer full on fd=%d", conn->fd);
            conn_close(conn);
            return;
        }

        ssize_t n = recv(conn->fd, conn->read_buf + conn->read_len, space, 0);
        if (n > 0) {
            conn->read_len += (size_t)n;
            conn->read_buf[conn->read_len] = '\0';

            if (strstr(conn->read_buf, "\r\n\r\n") != NULL) {
                build_default_response(conn);
                if (conn->state == CONN_STATE_WRITING) {
                    conn_handle_write(conn);
                }
                return;
            }
            continue;
        }
        if (n == 0) {
            conn_close(conn);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            timer_heap_upsert(&conn->server->timer_heap, &conn->timer,
                              conn->server->config.read_timeout_ms);
            return;
        }

        conn_close(conn);
        return;
    }
}

bool server_init(Server *srv, const ServerConfig *config) {
    if (!srv || !config || config->max_connections == 0) {
        return false;
    }

    memset(srv, 0, sizeof(*srv));
    srv->config = *config;
    srv->listen_fd = -1;
    srv->epoll_fd = -1;

    if (!mem_pool_init(&srv->conn_pool, sizeof(Connection), config->max_connections)) {
        LOG_ERROR("Failed to initialize connection pool");
        return false;
    }

    if (!timer_heap_init(&srv->timer_heap, config->max_connections)) {
        LOG_ERROR("Failed to initialize timer heap");
        mem_pool_destroy(&srv->conn_pool);
        return false;
    }

    srv->listen_fd = listen_socket_create(config->port);
    if (srv->listen_fd < 0) {
        server_destroy(srv);
        return false;
    }

    srv->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (srv->epoll_fd < 0) {
        LOG_ERROR("epoll_create1() failed: %s", strerror(errno));
        server_destroy(srv);
        return false;
    }

    struct epoll_event ev = {
        .events = EPOLLIN | EPOLLET,
        .data.ptr = NULL,
    };

    if (epoll_ctl(srv->epoll_fd, EPOLL_CTL_ADD, srv->listen_fd, &ev) < 0) {
        LOG_ERROR("epoll_ctl(ADD listen_fd) failed: %s", strerror(errno));
        server_destroy(srv);
        return false;
    }

    return true;
}

void server_run(Server *srv) {
    if (!srv) {
        return;
    }

    struct epoll_event events[MAX_EPOLL_EVENTS];
    srv->running = true;

    LOG_INFO("Server listening on port %u (max_connections=%zu)", srv->config.port,
             srv->config.max_connections);

    while (srv->running) {
        int timeout_ms = timer_heap_next_timeout_ms(&srv->timer_heap);
        int n = epoll_wait(srv->epoll_fd, events, MAX_EPOLL_EVENTS, timeout_ms);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("epoll_wait() failed: %s", strerror(errno));
            break;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.ptr == NULL) {
                accept_pending(srv);
                continue;
            }

            Connection *conn = (Connection *)events[i].data.ptr;
            uint32_t ev = events[i].events;

            if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                conn_close(conn);
                continue;
            }

            if ((ev & EPOLLIN) && conn->state == CONN_STATE_READING) {
                conn_handle_read(conn);
            } else if ((ev & EPOLLOUT) && conn->state == CONN_STATE_WRITING) {
                conn_handle_write(conn);
            }
        }

        timer_heap_process_expired(&srv->timer_heap);
    }
}

void server_stop(Server *srv) {
    if (srv) {
        srv->running = false;
    }
}

void server_destroy(Server *srv) {
    if (!srv) {
        return;
    }
    if (srv->epoll_fd >= 0) {
        close(srv->epoll_fd);
    }
    if (srv->listen_fd >= 0) {
        close(srv->listen_fd);
    }
    timer_heap_destroy(&srv->timer_heap);
    mem_pool_destroy(&srv->conn_pool);
    memset(srv, 0, sizeof(*srv));
}
