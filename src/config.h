#ifndef AHSD_CONFIG_H
#define AHSD_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "log.h"

#ifndef AHSD_DEFAULT_WWW_ROOT
#define AHSD_DEFAULT_WWW_ROOT "/var/www/ahsd"
#endif

typedef struct {
    uint16_t port;
    size_t worker_count;
    size_t max_connections;
    uint64_t read_timeout_ms;
    uint64_t write_timeout_ms;
    const char *www_root;
    const char *static_prefix;
    LogLevel log_level;
} AppConfig;

typedef enum {
    CONFIG_PARSE_OK = 0,
    CONFIG_PARSE_EXIT_OK,
    CONFIG_PARSE_ERROR,
} ConfigParseResult;

void config_init_defaults(AppConfig *cfg);
ConfigParseResult config_load(AppConfig *cfg, int argc, char **argv);

#endif
