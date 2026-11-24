#include "channel.h"

PlainWaiter::PlainWaiter(FiberID fiber_id) : fiber_id(fiber_id) {}

SelectState::SelectState(FiberID fiber_id) : fiber_id(fiber_id) {}

SelectWaiter::SelectWaiter(SelectState* state) : state(state) {}

FiberID Waiter::fiber_id() const {
    if (auto* p = std::get_if<PlainWaiter>(&inner)) {
        return p->fiber_id;
    } else if (auto* p = std::get_if<SelectWaiter>(&inner)) {
        return p->state->fiber_id;
    }
    CR_UNREACHABLE("unknown waiter kind");
}

bool Waiter::stale() const {
    if (auto* p = std::get_if<SelectWaiter>(&inner)) {
        return p->state->claimed;
    }
    return false;
}

std::optional<FiberID> Waiter::claim() {
    if (auto* p = std::get_if<PlainWaiter>(&inner)) {
        return p->fiber_id;
    } else if (auto* p = std::get_if<SelectWaiter>(&inner)) {
        if (p->state->claimed) {
            return std::nullopt;
        }
        p->state->claimed = true;
        return p->state->fiber_id;
    }
    CR_UNREACHABLE("unknown waiter kind");
}
