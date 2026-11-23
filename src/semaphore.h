#pragma once

#include "context.h"

class Semaphore {
public:
    Semaphore(Context& parent, size_t initial_count = 0);
    Semaphore(const Semaphore&) = delete;
    ~Semaphore();

    void acquire();
    void release();
    bool try_acquire();

private:
    Context& ctx;
    size_t count;
    std::deque<FiberID> waiters;
};
