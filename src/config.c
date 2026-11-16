#include "config.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_u16(const char *str, uint16_t *out) {
    if (!str || *str == '\0') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long val = strtoul(str, &end, 10);
    if (errno != 0 || *end != '\0' || val == 0 || val > UINT16_MAX) {
        return false;
    }
    *out = (uint16_t)val;
    return true;
}

static bool parse_size(const char *str, size_t *out, bool allow_zero) {
    if (!str || *str == '\0') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long long val = strtoull(str, &end, 10);
    if (errno != 0 || *end != '\0' || (!allow_zero && val == 0)) {
        return false;
    }
    *out = (size_t)val;
    return true;
}

static bool parse_u64(const char *str, uint64_t *out) {
    if (!str || *str == '\0') {
        return false;
    }
    char *end = NULL;
    errno = 0;
    unsigned long long val = strtoull(str, &end, 10);
    if (errno != 0 || *end != '\0' || val == 0) {
        return false;
    }
    *out = (uint64_t)val;
    return true;
}

static bool parse_log_level(const char *str, LogLevel *out) {
    if (!str) {
        return false;
    }
    if (strcmp(str, "debug") == 0) {
        *out = LOG_LEVEL_DEBUG;
        return true;
    }
    if (strcmp(str, "info") == 0) {
        *out = LOG_LEVEL_INFO;
        return true;
    }
    if (strcmp(str, "warn") == 0) {
        *out = LOG_LEVEL_WARN;
        return true;
    }
    if (strcmp(str, "error") == 0) {
        *out = LOG_LEVEL_ERROR;
        return true;
    }
    return false;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  -p, --port <port>           Listening TCP port (default: 8080, env: AHSD_PORT)\n"
            "  -w, --workers <count>       Worker processes, 0=auto (default: 0, env: AHSD_WORKERS)\n"
            "  -c, --max-conn <count>      Max connections per worker (default: 4096, env: AHSD_MAX_CONN)\n"
            "  -t, --timeout <ms>          Read/write timeout in ms (default: 5000, env: AHSD_TIMEOUT)\n"
            "  -r, --root <dir>            Static files root directory (default: %s, env: AHSD_ROOT)\n"
            "  -l, --log-level <level>     debug|info|warn|error (default: info, env: AHSD_LOG_LEVEL)\n"
            "  -h, --help                  Show this help message\n",
            prog, AHSD_DEFAULT_WWW_ROOT);
}

void config_init_defaults(AppConfig *cfg) {
    cfg->port = 8080;
    cfg->worker_count = 0;
    cfg->max_connections = 4096;
    cfg->read_timeout_ms = 5000;
    cfg->write_timeout_ms = 5000;
    cfg->www_root = AHSD_DEFAULT_WWW_ROOT;
    cfg->static_prefix = "/static";
    cfg->log_level = LOG_LEVEL_INFO;
}

static bool load_from_env(AppConfig *cfg) {
    const char *val;

    if ((val = getenv("AHSD_PORT")) && !parse_u16(val, &cfg->port)) {
        fprintf(stderr, "Invalid AHSD_PORT: %s\n", val);
        return false;
    }
    if ((val = getenv("AHSD_WORKERS")) && !parse_size(val, &cfg->worker_count, true)) {
        fprintf(stderr, "Invalid AHSD_WORKERS: %s\n", val);
        return false;
    }
    if ((val = getenv("AHSD_MAX_CONN")) && !parse_size(val, &cfg->max_connections, false)) {
        fprintf(stderr, "Invalid AHSD_MAX_CONN: %s\n", val);
        return false;
    }
    if ((val = getenv("AHSD_TIMEOUT"))) {
        uint64_t t = 0;
        if (!parse_u64(val, &t)) {
            fprintf(stderr, "Invalid AHSD_TIMEOUT: %s\n", val);
            return false;
        }
        cfg->read_timeout_ms = t;
        cfg->write_timeout_ms = t;
    }
    if ((val = getenv("AHSD_ROOT")) && *val != '\0') {
        cfg->www_root = val;
    }
    if ((val = getenv("AHSD_LOG_LEVEL")) && !parse_log_level(val, &cfg->log_level)) {
        fprintf(stderr, "Invalid AHSD_LOG_LEVEL: %s\n", val);
        return false;
    }
    return true;
}

ConfigParseResult config_load(AppConfig *cfg, int argc, char **argv) {
    if (!cfg) {
        return CONFIG_PARSE_ERROR;
    }

    config_init_defaults(cfg);
    if (!load_from_env(cfg)) {
        return CONFIG_PARSE_ERROR;
    }

    static const struct option long_opts[] = {
        {"port", required_argument, NULL, 'p'},
        {"workers", required_argument, NULL, 'w'},
        {"max-conn", required_argument, NULL, 'c'},
        {"timeout", required_argument, NULL, 't'},
        {"root", required_argument, NULL, 'r'},
        {"log-level", required_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:w:c:t:r:l:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'p':
                if (!parse_u16(optarg, &cfg->port)) {
                    fprintf(stderr, "Invalid port: %s\n", optarg);
                    return CONFIG_PARSE_ERROR;
                }
                break;
            case 'w':
                if (!parse_size(optarg, &cfg->worker_count, true)) {
                    fprintf(stderr, "Invalid worker count: %s\n", optarg);
                    return CONFIG_PARSE_ERROR;
                }
                break;
            case 'c':
                if (!parse_size(optarg, &cfg->max_connections, false)) {
                    fprintf(stderr, "Invalid max-conn: %s\n", optarg);
                    return CONFIG_PARSE_ERROR;
                }
                break;
            case 't': {
                uint64_t t = 0;
                if (!parse_u64(optarg, &t)) {
                    fprintf(stderr, "Invalid timeout: %s\n", optarg);
                    return CONFIG_PARSE_ERROR;
                }
                cfg->read_timeout_ms = t;
                cfg->write_timeout_ms = t;
                break;
            }
            case 'r':
                if (*optarg == '\0') {
                    fprintf(stderr, "Root directory cannot be empty\n");
                    return CONFIG_PARSE_ERROR;
                }
                cfg->www_root = optarg;
                break;
            case 'l':
                if (!parse_log_level(optarg, &cfg->log_level)) {
                    fprintf(stderr, "Invalid log level: %s\n", optarg);
                    return CONFIG_PARSE_ERROR;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return CONFIG_PARSE_EXIT_OK;
            default:
                print_usage(argv[0]);
                return CONFIG_PARSE_ERROR;
        }
    }

    return CONFIG_PARSE_OK;
}
