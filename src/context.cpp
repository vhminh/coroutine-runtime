#include "context.h"

#include "fiber.h"

#include <cassert>

Context::Context() {}

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
            swap_context(&this->sched_sp, fiber.sp, nullptr);
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
