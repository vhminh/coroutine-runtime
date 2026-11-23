#include "coroutine-runtime.h"

#include <liburing.h>

IOUringVersion get_io_uring_version() {
    return IOUringVersion{
        .major = io_uring_major_version(),
        .minor = io_uring_minor_version(),
    };
}
