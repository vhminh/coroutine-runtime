#pragma once

#include "def.h"
#include "fiber.h"

#include <deque>
#include <unordered_map>

void swap_context(void** save_sp, void* load_sp);

class Context {
public:
    Context();
    Context(const Context&) = delete;
    Context(Context&&) = delete;

    FiberID spawn(void (*func)(Context&));
    FiberID current_fiber_id() const;
    void run_loop();
    void yield();
    void sleep(long millis);

    void* sched_sp = nullptr;

private:
    size_t max_fiber_id = 0;
    std::unordered_map<uint64_t, Box<Fiber>> fibers;
    std::deque<uint64_t> active_fibers;
    Fiber& current();
};
