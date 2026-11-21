#pragma once

#include "context.h"

namespace cr {
struct Version {
    int major, minor;
};

Version get_uring_version();
}; // namespace cr
