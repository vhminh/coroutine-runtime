#include "select.h"

#include <algorithm>

Selector::Selector(Context& ctx) : ctx(ctx) {}

std::optional<size_t> Selector::select() {
    auto& fiber = this->ctx.current();
    while (true) {
        // check for already available values
        for (size_t i = 0; i < this->branches.size(); ++i) {
            SelectBranch& branch = this->branches[i];
            if (branch.closed) {
                continue;
            }
            auto res = branch.try_now();
            if (res == RecvStatus::Ok) {
                return i;
            } else if (res == RecvStatus::Closed) {
                branch.closed = true;
                return i;
            }
        }
        // check for completion
        bool all_closed = std::all_of(this->branches.begin(), this->branches.end(), [](auto& b) { return b.closed; });
        if (all_closed) {
            return std::nullopt;
        }
        // wait
        WakeToken token(fiber.id);
        for (auto& branch : this->branches) {
            if (branch.closed) {
                continue;
            }
            branch.enqueue(&token);
        }
        this->ctx.park(fiber);
        for (auto& branch : this->branches) {
            if (branch.closed) {
                continue;
            }
            branch.dequeue(&token);
        }
    }
}
