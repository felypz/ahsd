#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static LogLevel g_min_level = LOG_LEVEL_INFO;
static bool g_use_color = false;

static const char *level_names[] = {
    [LOG_LEVEL_DEBUG] = "DEBUG",
    [LOG_LEVEL_INFO] = "INFO ",
    [LOG_LEVEL_WARN] = "WARN ",
    [LOG_LEVEL_ERROR] = "ERROR",
};

static const char *level_colors[] = {
    [LOG_LEVEL_DEBUG] = "\x1b[36m",
    [LOG_LEVEL_INFO] = "\x1b[32m",
    [LOG_LEVEL_WARN] = "\x1b[33m",
    [LOG_LEVEL_ERROR] = "\x1b[31m",
};

void log_init(LogLevel min_level) {
    g_min_level = min_level;
    g_use_color = isatty(STDERR_FILENO) != 0;
}

void log_set_level(LogLevel min_level) {
    g_min_level = min_level;
}

void log_write(LogLevel level, const char *file, int line, const char *fmt, ...) {
    if (level < g_min_level) {
        return;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_info);

    pid_t pid = getpid();

    if (g_use_color) {
        fprintf(stderr, "%s.%03ld [pid:%d] %s%s\x1b[0m %s:%d: ", time_buf, ts.tv_nsec / 1000000L,
                (int)pid, level_colors[level], level_names[level], file, line);
    } else {
        fprintf(stderr, "%s.%03ld [pid:%d] %s %s:%d: ", time_buf, ts.tv_nsec / 1000000L, (int)pid,
                level_names[level], file, line);
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fputc('\n', stderr);
}
