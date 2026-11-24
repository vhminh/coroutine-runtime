#include "waiter.h"

#include "check.h"
#include "context.h"

WakeToken::WakeToken(FiberID fiber_id) : fiber_id(fiber_id) {}

void WaitQueue::push(WakeToken* t) {
    CR_ASSERT(t);
    this->q.push_back(t);
}

void WaitQueue::remove(WakeToken* t) {
    CR_ASSERT(t);
    // O(n) for now
    std::erase(q, t);
}

void WaitQueue::wake_one(Context& c) {
    while (this->q.size() && this->q.front()->claimed) {
        this->q.pop_front();
    }
    if (this->q.size()) {
        auto* token = this->q.front();
        this->q.pop_front();
        CR_ASSERT(!token->claimed, "expect non stale waiter");
        token->claimed = true;
        auto fiber_id = token->fiber_id;
        c.unpark(*c.fibers.at(fiber_id));
    }
}

void WaitQueue::wake_all(Context& c) {
    while (this->q.size()) {
        auto* token = this->q.front();
        this->q.pop_front();
        if (!token->claimed) {
            token->claimed = true;
            auto fiber_id = token->fiber_id;
            c.unpark(*c.fibers.at(fiber_id));
        }
    }
}

bool WaitQueue::empty() {
    while (this->q.size() && this->q.front()->claimed) {
        this->q.pop_front();
    }
    return this->q.empty();
}
