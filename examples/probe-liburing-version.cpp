#include "coroutine-runtime.h"

#include <iostream>

int main() {
    const auto version = cr::get_liburing_header_version();
    std::cout << "liburing version: (" << version.major << "," << version.minor << ")" << std::endl;
    return 0;
}
