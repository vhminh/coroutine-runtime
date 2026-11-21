#include "context.h"

#include "fiber.h"

#include <cassert>

Context::Context() {}

Fiber& Context::current() {
    assert(this->active_fibers.size() > 0);
    FiberID id = this->active_fibers.front();
    return *fibers[id];
}

FiberID Context::current_fiber_id() const {
    if (this->active_fibers.size()) {
        return this->active_fibers.front();
    }
    return -1;
}

FiberID Context::spawn(void (*func)(Context&)) {
    Stack stack; // TODO: pool stacks
    FiberID id = ++this->max_fiber_id;
    Box<Fiber> fiber = std::make_unique<Fiber>(*this, id, std::move(stack), func);
    fibers[id] = std::move(fiber);
    this->active_fibers.push_back(id);
    return id;
}

void Context::run_loop() {
    while (active_fibers.size()) {
        auto& fiber = this->current();
        swap_context(&this->sched_sp, fiber.sp);
        switch (fiber.state) {
        case FiberState::Ready:
            // yielded
            active_fibers.pop_front();
            active_fibers.push_back(fiber.id);
            break;
        case FiberState::Finish:
            // finished
            active_fibers.pop_front();
            break;
        }
    }
}

void Context::yield() {
    Fiber& current = this->current();
    current.state = FiberState::Ready;
    swap_context(&current.sp, this->sched_sp);
}
