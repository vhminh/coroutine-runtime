#include "context.h"

#include "fiber.h"

#include <array>
#include <cassert>
#include <utility>

Context::Context() {
    int ret = io_uring_queue_init(IO_URING_QUEUE_DEPTH, &this->uring, 0);
    assert(ret == 0);
}

Context::~Context() {
    io_uring_queue_exit(&this->uring);
}

Fiber& Context::current() {
    assert(this->ready.size() > 0);
    FiberID id = this->ready.front();
    return *fibers[id];
}

FiberID Context::current_fiber_id() const {
    if (this->ready.size()) {
        return this->ready.front();
    }
    return -1;
}

FiberID Context::spawn(std::function<void(Context&)> func) {
    Stack stack; // TODO: pool stacks
    FiberID id = ++this->max_fiber_id;
    fibers[id] = std::make_unique<Fiber>(*this, id, std::move(stack), func);
    this->ready.push_back(id);
    return id;
}

void Context::run_loop() {
    while (this->fibers.size()) {
        // check timers
        while (this->timers.size() && this->timers.top().first <= Clock::now()) {
            FiberID fiber_id = this->timers.top().second;
            this->timers.pop();
            assert(this->fibers[fiber_id]->state == FiberState::Blocked);
            this->fibers[fiber_id]->state = FiberState::Ready;
            this->ready.push_back(fiber_id);
        }
        // check IO events
        {
            io_uring_cqe* cqe;
            unsigned head, i = 0;
            io_uring_for_each_cqe(&this->uring, head, cqe) {
                FiberID fiber_id = reinterpret_cast<FiberID>(io_uring_cqe_get_data(cqe));
                Fiber& fiber = *this->fibers.at(fiber_id);
                assert(fiber.state == FiberState::Blocked);
                fiber.state = FiberState::Ready;
                fiber.resume_value = reinterpret_cast<void*>(static_cast<intptr_t>(cqe->res));
                this->ready.push_back(fiber_id);
                i++;
            }

            io_uring_cq_advance(&this->uring, i);
        }
        // process any ready fibers
        for (int count = this->ready.size(); count > 0; --count) {
            auto& fiber = this->current();
            assert(fiber.state == FiberState::Ready);
            swap_context(&this->sched_sp, fiber.sp, fiber.resume_value);
            if (fiber.state == FiberState::Finished) {
                this->fibers.erase(fiber.id);
            }
        }
        // wait for timers or IO events
        if (this->fibers.size() && this->ready.empty()) {
            __kernel_timespec ts;
            __kernel_timespec* tsp = nullptr; // wait forever
            if (!this->timers.empty()) {
                auto duration = this->timers.top().first - Clock::now();
                auto ns = std::max<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(), 0);
                ts = {.tv_sec = ns / 1'000'000'000, .tv_nsec = ns % 1'000'000'000};
                tsp = &ts;
            }
            io_uring_cqe* cqe;
            int ret = io_uring_wait_cqe_timeout(&this->uring, &cqe, tsp);
            assert(ret == 0 || ret == -ETIME);
        }
    }
}

void Context::run_fiber(Fiber& fiber) {
    fiber.func(fiber.parent);
    fiber.state = FiberState::Finished;

    fiber.parent.ready.pop_front();

    swap_context(&fiber.sp, fiber.parent.sched_sp, nullptr);
    __builtin_unreachable();
}

void Context::yield() {
    Fiber& fiber = this->current();
    fiber.state = FiberState::Ready;

    this->ready.pop_front();
    this->ready.push_back(fiber.id);

    swap_context(&fiber.sp, this->sched_sp, nullptr);
}

void Context::sleep(Clock::duration duration) {
    Fiber& fiber = this->current();
    fiber.state = FiberState::Blocked;

    this->ready.pop_front();
    this->timers.push({Clock::now() + duration, fiber.id});

    swap_context(&fiber.sp, this->sched_sp, nullptr);
}

int Context::accept(int fd, sockaddr* addr, socklen_t* len, int flags) {
    Fiber& fiber = this->current();
    fiber.state = FiberState::Blocked;

    this->ready.pop_front();

    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_accept(sqe, fd, addr, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));
    io_uring_submit(&this->uring);

    return static_cast<int>(reinterpret_cast<intptr_t>(swap_context(&fiber.sp, this->sched_sp, nullptr)));
}

int Context::recv(int fd, void* buf, size_t len, int flags) {
    Fiber& fiber = this->current();
    fiber.state = FiberState::Blocked;

    this->ready.pop_front();

    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_recv(sqe, fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));
    io_uring_submit(&this->uring);

    return static_cast<int>(reinterpret_cast<intptr_t>(swap_context(&fiber.sp, this->sched_sp, nullptr)));
}
int Context::send(int fd, const void* buf, size_t len, int flags) {
    Fiber& fiber = this->current();
    fiber.state = FiberState::Blocked;

    this->ready.pop_front();

    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_send(sqe, fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));
    io_uring_submit(&this->uring);

    return static_cast<int>(reinterpret_cast<intptr_t>(swap_context(&fiber.sp, this->sched_sp, nullptr)));
}
