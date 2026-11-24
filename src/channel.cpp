#include "channel.h"

PlainWaiter::PlainWaiter(FiberID fiber_id) : fiber_id(fiber_id) {}

SelectState::SelectState(FiberID fiber_id) : fiber_id(fiber_id) {}

SelectWaiter::SelectWaiter(SelectState* sel, size_t branch_index) : sel(sel), branch_index(branch_index) {}

FiberID Waiter::fiber_id() const {
    if (auto* p = std::get_if<PlainWaiter>(&inner)) {
        return p->fiber_id;
    } else if (auto* p = std::get_if<SelectWaiter>(&inner)) {
        return p->sel->fiber_id;
    }
    CR_UNREACHABLE("unknown waiter kind");
}

bool Waiter::stale() const {
    if (auto* p = std::get_if<SelectWaiter>(&inner)) {
        return p->sel->woken;
    }
    return false;
}

std::optional<FiberID> Waiter::notify() {
    if (auto* p = std::get_if<PlainWaiter>(&inner)) {
        return p->fiber_id;
    } else if (auto* p = std::get_if<SelectWaiter>(&inner)) {
        if (p->sel->woken) {
            return std::nullopt;
        }
        p->sel->woken = true;
        p->sel->fired_index = p->branch_index;
        return p->sel->fiber_id;
    }
    CR_UNREACHABLE("unknown waiter kind");
}
