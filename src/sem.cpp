#include "sem.h"

#include "check.h"

Semaphore::Semaphore(Context& parent, size_t initial_count) : ctx(parent), count(initial_count) {}

Semaphore::~Semaphore() {
    CR_ASSERT(this->waiters.empty(), "freeing a semaphore with {} waiters", this->waiters.size());
}

void Semaphore::acquire() {
    auto& fiber = this->ctx.current();
    if (this->count > 0) {
        --this->count;
        return;
    }
    this->waiters.push_back(fiber.id);

    this->ctx.park(fiber);
}

void Semaphore::release() {
    CR_ASSERT(this->ctx.running, "must be running inside a fiber");
    if (this->waiters.empty()) {
        this->count++;
        return;
    }
    CR_ASSERT(this->count == 0);
    auto fiber_id = this->waiters.front();
    this->waiters.pop_front();

    auto& fiber = *this->ctx.fibers.at(fiber_id);
    this->ctx.unpark(fiber);
}

bool Semaphore::try_acquire() {
    CR_ASSERT(this->ctx.running, "must be running inside a fiber");
    if (this->count > 0) {
        --this->count;
        return true;
    }
    return false;
}
