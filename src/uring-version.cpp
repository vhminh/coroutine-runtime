#include "coroutine-runtime.h"

#include <liburing.h>

namespace cr {

Version get_uring_version() {
    return Version{
        .major = io_uring_major_version(),
        .minor = io_uring_minor_version(),
    };
}
}; // namespace cr
