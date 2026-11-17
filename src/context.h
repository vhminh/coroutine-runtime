#pragma once

#include "def.h"
#include "fiber.h"

#include <vector>

class Context {
public:
    Context();
    Context(const Context&) = delete;
    Context(Context&&) = delete;

    const Fiber& spawn(void (*func)(Context&));
    void run_loop();
    void yield();
    void sleep(long millis);

private:
    Fiber const* running = nullptr;
    size_t num_fibers = 0;
    std::vector<Box<Fiber>> fibers;
};
