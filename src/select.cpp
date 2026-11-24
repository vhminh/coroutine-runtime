#include "select.h"

#include <algorithm>

Selector::Selector(Context& ctx) : ctx(ctx) {}

size_t Selector::select() {
    auto& fiber = this->ctx.current();
    SelectState state(fiber.id);
    while (true) {
        // check for already available values
        for (auto& branch : this->branches) {
            size_t index = branch.index;
            auto res = branch.try_now();
            if (res == SelectTryStatus::Ok) {
                return index;
            } else if (res == SelectTryStatus::Closed && !this->closed[index]) {
                this->closed[index] = true;
                return index;
            }
        }
        // check for completion
        bool all_closed = std::all_of(this->closed.begin(), this->closed.end(), [](bool b) { return b; });
        if (all_closed) {
            return (size_t)-1;
        }
        // wait
        for (auto& branch : this->branches) {
            branch.enqueue(&state);
        }
        this->ctx.park(fiber);
        for (auto& branch : this->branches) {
            branch.dequeue(&state);
        }
        // try again
        size_t index = state.fired_index;
        auto res = this->branches[index].try_now();
        if (res == SelectTryStatus::Ok) {
            return index;
        } else if (res == SelectTryStatus::Closed && !this->closed[index]) {
            this->closed[index] = true;
            return index;
        }
    }
}
