#pragma once

#include "channel.h"
#include "context.h"
#include "select.h"
#include "sem.h"

struct IOUringVersion {
    int major, minor;
};

IOUringVersion get_io_uring_version();
