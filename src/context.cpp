#include "context.h"

#include "check.h"
#include "fiber.h"

Context::Context() {
    int ret = io_uring_queue_init(IO_URING_QUEUE_DEPTH, &this->uring, 0);
    if (ret != 0) {
        throw SystemError(-ret, "io_uring_queue_init, depth={}", IO_URING_QUEUE_DEPTH);
    }
}

Context::~Context() {
    io_uring_queue_exit(&this->uring);
}

Fiber& Context::current() {
    CR_ASSERT(this->running != nullptr);
    return *this->running;
}

FiberID Context::current_fiber_id() const {
    CR_ASSERT(this->running != nullptr, "must be running inside a fiber");
    return this->running->id;
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
            auto fiber_id = this->timers.top().second;
            this->timers.pop();
            this->unpark(*this->fibers.at(fiber_id));
        }
        // check IO events
        {
            io_uring_cqe* cqe;
            unsigned head, i = 0;
            io_uring_for_each_cqe(&this->uring, head, cqe) {
                auto fiber_id = reinterpret_cast<FiberID>(io_uring_cqe_get_data(cqe));
                auto& fiber = *this->fibers.at(fiber_id);
                this->unpark(fiber, reinterpret_cast<void*>(static_cast<intptr_t>(cqe->res)));
                i++;
            }

            io_uring_cq_advance(&this->uring, i);
        }
        // process any ready fibers
        for (int count = this->ready.size(); count > 0; --count) {
            auto& fiber = *this->fibers.at(this->ready.front());
            this->ready.pop_front();
            CR_ASSERT(fiber.state == FiberState::Ready);
            void* resume_value = fiber.resume_value;
            fiber.resume_value = nullptr;
            this->running = &fiber;
            swap_context(&this->sched_sp, fiber.sp, resume_value);
            this->running = nullptr;
            if (fiber.state == FiberState::Finished) {
                auto id = fiber.id;
                this->fibers.erase(id);
            }
        }
        // wait for timers or IO events
        if (this->fibers.size() && this->ready.empty()) {
            if (!this->timers.empty()) {
                auto duration = this->timers.top().first - Clock::now();
                auto ns = std::max<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count(), 0);
                __kernel_timespec ts = {.tv_sec = ns / 1'000'000'000, .tv_nsec = ns % 1'000'000'000};
                io_uring_cqe* cqe;
                int ret = io_uring_submit_and_wait_timeout(&this->uring, &cqe, 1, &ts, nullptr);
                if (ret < 0 && ret != -ETIME && ret != -EINTR && ret != -EAGAIN) {
                    throw SystemError(-ret, "io_uring_submit_and_wait_timeout");
                }
            } else {
                int ret = io_uring_submit_and_wait(&this->uring, 1);
                if (ret < 0 && ret != -EINTR && ret != -EAGAIN) {
                    throw SystemError(-ret, "io_uring_submit_and_wait");
                }
            }
        } else {
            int ret = io_uring_submit(&this->uring);
            if (ret < 0) {
                throw SystemError(-ret, "io_uring_submit");
            }
        }
    }
}

void Context::dump_state(std::ostream& out) {
    out << "fibers:" << std::endl;
    for (auto& p : this->fibers) {
        const char* state = "<unknown>";
        switch (p.second->state) {
        case FiberState::Ready:
            state = "ready";
            break;
        case FiberState::Blocked:
            state = "blocked";
            break;
        case FiberState::Finished:
            state = "finished";
            break;
        }
        out << "  fiber " << p.first << " " << state << "\n";
    }
    out << std::endl;
    out << "timers:" << std::endl;
    auto copy = this->timers;
    auto now = Clock::now();
    while (copy.size()) {
        auto [deadline, id] = copy.top();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        out << "  fiber " << id << " in " << ms << " ms\n";
        copy.pop();
    }
    out << std::endl;
}

void Context::run_fiber(Fiber& fiber) {
    try {
        fiber.func(fiber.parent);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "coroutine-runtime: fiber %lu threw: %s\n", fiber.id, e.what());
    } catch (...) {
        std::fprintf(stderr, "coroutine-runtime: fiber %lu threw a non-std exception\n", fiber.id);
    }
    fiber.parent.exit(fiber);
    __builtin_unreachable();
}

void* Context::park(Fiber& fiber) {
    CR_ASSERT(fiber.state == FiberState::Ready);
    fiber.state = FiberState::Blocked;
    return swap_context(&fiber.sp, this->sched_sp, nullptr);
}
void Context::unpark(Fiber& fiber, void* resume_val) {
    CR_ASSERT(fiber.state == FiberState::Blocked);
    fiber.state = FiberState::Ready;
    fiber.resume_value = resume_val;
    this->ready.push_back(fiber.id);
}

void Context::yield(Fiber& fiber) {
    CR_ASSERT(fiber.state == FiberState::Ready);
    fiber.resume_value = nullptr;
    this->ready.push_back(fiber.id);
    swap_context(&fiber.sp, this->sched_sp, nullptr);
}

[[noreturn]]
void Context::exit(Fiber& fiber) {
    CR_ASSERT(fiber.state == FiberState::Ready);
    fiber.state = FiberState::Finished;
    swap_context(&fiber.sp, fiber.parent.sched_sp, nullptr);
    __builtin_unreachable();
}

void Context::yield() {
    auto& fiber = this->current();
    this->yield(fiber);
}

void Context::sleep(Clock::duration duration) {
    auto& fiber = this->current();

    this->timers.push({Clock::now() + duration, fiber.id});

    this->park(fiber);
}

io_uring_sqe* get_sqe(io_uring* ring) {
    io_uring_sqe* sqe = io_uring_get_sqe(ring);
    if (sqe == nullptr) {
        throw CRError("io_uring queue full");
    }
    return sqe;
}

int Context::block_on(io_uring_sqe* sqe) {
    CR_ASSERT(sqe);
    auto& fiber = this->current();

    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));

    void* ret = this->park(fiber);
    return static_cast<int>(reinterpret_cast<intptr_t>(ret));
}

int Context::accept(int fd, sockaddr* addr, socklen_t* len, int flags) {
    io_uring_sqe* sqe = get_sqe(&this->uring);
    io_uring_prep_accept(sqe, fd, addr, len, flags);
    return this->block_on(sqe);
}

int Context::recv(int fd, void* buf, size_t len, int flags) {
    io_uring_sqe* sqe = get_sqe(&this->uring);
    io_uring_prep_recv(sqe, fd, buf, len, flags);
    return this->block_on(sqe);
}

int Context::send(int fd, const void* buf, size_t len, int flags) {
    io_uring_sqe* sqe = get_sqe(&this->uring);
    io_uring_prep_send(sqe, fd, buf, len, flags);
    return this->block_on(sqe);
}

int Context::close(int fd) {
    io_uring_sqe* sqe = get_sqe(&this->uring);
    io_uring_prep_close(sqe, fd);
    return this->block_on(sqe);
}
