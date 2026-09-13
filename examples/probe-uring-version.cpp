#include "coroutine-runtime.h"

#include <iostream>

int main() {
    const auto version = cr::get_uring_version();
    std::cout << "io_uring version: (" << version.major << "," << version.minor << ")" << std::endl;
    return 0;
}
