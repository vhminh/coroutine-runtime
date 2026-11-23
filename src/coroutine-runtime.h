#pragma once

#include "context.h"

struct IOUringVersion {
    int major, minor;
};

IOUringVersion get_io_uring_version();
