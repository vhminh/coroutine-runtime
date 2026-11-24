#pragma once

#include "channel.h"

struct SelectBranch {
    std::function<RecvStatus(void)> try_now;
    std::function<void(WakeToken*)> enqueue;
    std::function<void(WakeToken*)> dequeue;
    bool closed = false;
};

class Selector {
public:
    Selector(Context& ctx);
    Selector(const Selector&) = delete;
    Selector& operator=(const Selector&) = delete;

    template <class T>
    size_t recv(const Receiver<T>& chann, std::optional<T>& out) {
        CR_ASSERT(&chann.impl->ctx == &this->ctx);
        size_t index = branches.size();
        auto ch = chann.impl;
        SelectBranch branch = SelectBranch{
            .try_now = [ch, &out]() { return ch->try_recv(out); },
            .enqueue = [ch](WakeToken* token) { ch->add_recv_waiter(token); },
            .dequeue = [ch](WakeToken* token) { ch->remove_waiters(token); }
        };
        branches.emplace_back(std::move(branch));
        return index;
    }

    std::optional<size_t> select();

private:
    Context& ctx;
    std::vector<SelectBranch> branches;
};
