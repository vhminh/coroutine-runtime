#pragma once

#include "def.h"
#include "fiber.h"

#include <chrono>
#include <deque>
#include <iosfwd>
#include <liburing.h>
#include <linux/openat2.h>
#include <queue>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>

const size_t IO_URING_QUEUE_DEPTH = 2048;

extern "C" [[noreturn]] void fiber_start(Fiber* f);
extern "C" void* swap_context(void** save_sp, void* load_sp, void* ret_val);

class Context {
    friend void fiber_start(Fiber*);
    friend class Semaphore;
    friend class Selector;
    friend class WaitQueue;
    template <class T> friend class ChanImpl;

public:
    Context();
    Context(const Context&) = delete;
    Context(Context&&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context();

    using Clock = std::chrono::steady_clock;

    FiberID spawn(std::function<void(Context&)>);
    FiberID current_fiber_id() const;
    void run_loop();
    void yield();
    void sleep(Clock::duration);
    int accept(int fd, sockaddr* addr, socklen_t* len, int flags);
    int recv(int fd, void* buf, size_t len, int flags);
    int send(int fd, const void* buf, size_t len, int flags);
    int close(int fd);
    int openat2(int fd, const char* path, open_how* how);
    int read(int fd, void* buf, unsigned len, uint64_t offset);
    int statx(int fd, const char* path, int flags, unsigned mask, struct statx* statxbuf);

private:
    [[noreturn]] static void run_fiber(Fiber& f);
    void* park(Fiber&);
    void unpark(Fiber&, void* resume_val = nullptr);
    void yield(Fiber&);
    [[noreturn]] void exit(Fiber&);
    int block_on(io_uring_sqe*);
    void* sched_sp = nullptr;
    Fiber* running = nullptr;
    Fiber& current();

    FiberID max_fiber_id = 0;
    std::unordered_map<FiberID, Box<Fiber>> fibers;
    std::deque<FiberID> ready;

    using Timer = std::pair<Clock::time_point, FiberID>;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers;

    io_uring uring;

    void dump_state(std::ostream&);
};
