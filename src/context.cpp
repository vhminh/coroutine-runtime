#include "context.h"

#include "fiber.h"

#include <cassert>

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
        while (this->ready.size()) {
            auto& fiber = this->current();
            assert(fiber.state == FiberState::Ready);
            swap_context(&this->sched_sp, fiber.sp);
            if (fiber.state == FiberState::Finished) {
                this->fibers.erase(fiber.id);
            }
        }
        while (this->timers.size() && this->timers.top().first <= Clock::now()) {
            FiberID fiber_id = this->timers.top().second;
            this->timers.pop();
            assert(this->fibers[fiber_id]->state == FiberState::Blocked);
            this->fibers[fiber_id]->state = FiberState::Ready;
            this->ready.push_back(fiber_id);
        }
        io_uring_cqe* cqes = nullptr;
        int count;
        while ((count = io_uring_peek_batch_cqe(&this->uring, &cqes, 10)) > 0) {
            for (int i = 0; i < count; ++i) {
                io_uring_cqe* cqe = cqes + i;
                FiberID fiber_id = reinterpret_cast<FiberID>(io_uring_cqe_get_data(cqe));
                io_uring_cqe_seen(&this->uring, cqe);
                this->ready.push_front(fiber_id);
            }
        }
    }
}

void Context::run_fiber(Fiber& fiber) {
    fiber.func(fiber.parent);
    fiber.state = FiberState::Finished;

    fiber.parent.ready.pop_front();

    swap_context(&fiber.sp, fiber.parent.sched_sp);
    __builtin_unreachable();
}

void Context::yield() {
    Fiber& fiber = this->current();
    fiber.state = FiberState::Ready;

    this->ready.pop_front();
    this->ready.push_back(fiber.id);

    swap_context(&fiber.sp, this->sched_sp);
}

void Context::sleep(Clock::duration duration) {
    Fiber& fiber = this->current();
    fiber.state = FiberState::Blocked;

    this->ready.pop_front();
    this->timers.push({Clock::now() + duration, fiber.id});

    swap_context(&fiber.sp, this->sched_sp);
}

void Context::accept(int fd, sockaddr* addr, socklen_t* len, int flags) {
    Fiber& fiber = this->current();
    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_accept(sqe, fd, addr, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));
    io_uring_submit(&this->uring);
}

void Context::recv(int fd, void* buf, size_t len, int flags) {
    Fiber& fiber = this->current();
    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_recv(sqe, fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));
    io_uring_submit(&this->uring);
}
void Context::send(int fd, const void* buf, size_t len, int flags) {
    Fiber& fiber = this->current();
    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_send(sqe, fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));
    io_uring_submit(&this->uring);
}
