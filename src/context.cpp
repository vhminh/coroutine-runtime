#include "context.h"

#include "fiber.h"

#include <cassert>

Semaphore::Semaphore(Context& parent, SemaphoreID id, size_t initial_count) :
    parent(parent), id(id), count(initial_count) {}

Context::Context() {
    int ret = io_uring_queue_init(IO_URING_QUEUE_DEPTH, &this->uring, 0);
    assert(ret == 0);
}

Context::~Context() {
    io_uring_queue_exit(&this->uring);
}

Fiber& Context::current() {
    assert(this->running != nullptr);
    return *this->running;
}

FiberID Context::current_fiber_id() const {
    assert(this->running != nullptr && "not running inside a fiber");
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
            auto& fiber = *this->fibers.at(this->ready.front());
            this->ready.pop_front();
            assert(fiber.state == FiberState::Ready);
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
                assert(ret >= 0 || ret == -ETIME || ret == -EINTR);
            } else {
                int ret = io_uring_submit_and_wait(&this->uring, 1);
                assert(ret >= 0 || ret == -EINTR);
            }
        } else {
            io_uring_submit(&this->uring);
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
    fiber.func(fiber.parent);

    fiber.parent.mark_finished(fiber);
    swap_context(&fiber.sp, fiber.parent.sched_sp, nullptr);
    __builtin_unreachable();
}

void Context::mark_ready(Fiber& fiber) {
    assert(fiber.state == FiberState::Ready || fiber.state == FiberState::Blocked);
    fiber.state = FiberState::Ready;
    this->ready.push_back(fiber.id);
}

void Context::mark_blocked(Fiber& fiber) {
    assert(fiber.state == FiberState::Ready);
    fiber.state = FiberState::Blocked;
}

void Context::mark_finished(Fiber& fiber) {
    assert(fiber.state == FiberState::Ready);
    fiber.state = FiberState::Finished;
}

void Context::yield() {
    auto& fiber = this->current();
    this->mark_ready(fiber);
    swap_context(&fiber.sp, this->sched_sp, nullptr);
}

void Context::sleep(Clock::duration duration) {
    auto& fiber = this->current();

    this->timers.push({Clock::now() + duration, fiber.id});

    this->mark_blocked(fiber);
    swap_context(&fiber.sp, this->sched_sp, nullptr);
}

int Context::accept(int fd, sockaddr* addr, socklen_t* len, int flags) {
    auto& fiber = this->current();
    this->mark_blocked(fiber);

    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_accept(sqe, fd, addr, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));

    return static_cast<int>(reinterpret_cast<intptr_t>(swap_context(&fiber.sp, this->sched_sp, nullptr)));
}

int Context::recv(int fd, void* buf, size_t len, int flags) {
    auto& fiber = this->current();

    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_recv(sqe, fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));

    this->mark_blocked(fiber);
    return static_cast<int>(reinterpret_cast<intptr_t>(swap_context(&fiber.sp, this->sched_sp, nullptr)));
}
int Context::send(int fd, const void* buf, size_t len, int flags) {
    auto& fiber = this->current();

    io_uring_sqe* sqe = io_uring_get_sqe(&this->uring);
    assert(sqe != nullptr);
    io_uring_prep_send(sqe, fd, buf, len, flags);
    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(fiber.id));

    this->mark_blocked(fiber);
    return static_cast<int>(reinterpret_cast<intptr_t>(swap_context(&fiber.sp, this->sched_sp, nullptr)));
}

SemaphoreID Context::new_semaphore(size_t initial_count) {
    SemaphoreID id = ++this->max_sem_id;
    this->semaphores[id] = std::make_unique<Semaphore>(*this, id, initial_count);
    return id;
}

void Context::free_semaphore(SemaphoreID id) {
    auto& sem = this->semaphores.at(id);
    if (sem) {
        assert(sem->waiters.empty());
        this->semaphores.erase(id);
    }
}

void Context::acquire(SemaphoreID id) {
    auto& sem = *this->semaphores.at(id);
    if (sem.count > 0) {
        --sem.count;
        return;
    }
    auto& fiber = this->current();
    sem.waiters.push_back(fiber.id);

    this->mark_blocked(fiber);
    swap_context(&fiber.sp, this->sched_sp, nullptr);
}

void Context::release(SemaphoreID id) {
    auto& sem = *this->semaphores.at(id);
    if (sem.waiters.empty()) {
        sem.count++;
        return;
    }
    assert(sem.count == 0);
    auto fiber_id = sem.waiters.front();
    sem.waiters.pop_front();

    auto& fiber = *this->fibers.at(fiber_id);
    this->mark_ready(fiber);
}

bool Context::try_acquire(SemaphoreID id) {
    auto& sem = *this->semaphores.at(id);
    if (sem.count > 0) {
        --sem.count;
        return true;
    }
    return false;
}
