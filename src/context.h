#pragma once

#include "def.h"
#include "fiber.h"

#include <chrono>
#include <deque>
#include <liburing.h>
#include <queue>
#include <sys/socket.h>
#include <unordered_map>

const size_t IO_URING_QUEUE_DEPTH = 2048;

extern "C" [[noreturn]] void fiber_start(Fiber* f);
extern "C" void swap_context(void** save_sp, void* load_sp);

class Context {
    friend void fiber_start(Fiber*);

public:
    Context();
    Context(const Context&) = delete;
    Context(Context&&) = delete;
    ~Context();

    using Clock = std::chrono::steady_clock;

    FiberID spawn(std::function<void(Context&)>);
    FiberID current_fiber_id() const;
    void run_loop();
    void yield();
    void sleep(Clock::duration);
    void accept(int fd, sockaddr* addr, socklen_t* len, int flags);
    void recv(int fd, void* buf, size_t len, int flags);
    void send(int fd, const void* buf, size_t len, int flags);

private:
    FiberID max_fiber_id = 0;
    std::unordered_map<FiberID, Box<Fiber>> fibers;
    std::deque<FiberID> ready;

    using Timer = std::pair<Clock::time_point, FiberID>;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers;
    Fiber& current();

    io_uring uring;

    [[noreturn]] static void run_fiber(Fiber& f);
    void* sched_sp = nullptr;
};
