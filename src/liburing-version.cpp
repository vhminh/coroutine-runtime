#include "coroutine-runtime.h"

#include <liburing.h>

#ifndef IO_URING_VERSION_MAJOR
#define IO_URING_VERSION_MAJOR 0
#define IO_URING_VERSION_MINOR 0
#endif

namespace cr {

Version get_liburing_header_version() {
    return Version{
        .major = IO_URING_VERSION_MAJOR,
        .minor = IO_URING_VERSION_MINOR,
    };
}
}; // namespace cr
