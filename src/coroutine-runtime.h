#pragma once

#include "context.h"
#include "sem.h"

struct IOUringVersion {
    int major, minor;
};

IOUringVersion get_io_uring_version();
