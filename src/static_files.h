#ifndef AHSD_STATIC_FILES_H
#define AHSD_STATIC_FILES_H

#include <stdbool.h>

#include "server.h"

typedef struct {
    const char *url_prefix;
    const char *root_dir;
} StaticMount;

bool server_mount_static(Server *srv, const StaticMount *mount);

#endif
