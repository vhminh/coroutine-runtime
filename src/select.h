#pragma once

#include "channel.h"


enum class SelectTryStatus {
    Ok,
    NotReady,
    Closed,
};

struct SelectBranch {
    std::function<SelectTryStatus(void)> try_now;
    std::function<void(SelectState*)> enqueue;
    std::function<void(SelectState*)> dequeue;
    bool closed = false;
};

class Selector {
public:
    Selector(Context& ctx);
    Selector(const Selector&) = delete;
    Selector& operator=(const Selector&) = delete;

    template <class T>
    size_t recv(Receiver<T> chann, std::optional<T>& out) {
        CR_ASSERT(&chann.impl->ctx == &this->ctx);
        size_t index = branches.size();
        SelectBranch branch = SelectBranch{
            .try_now =
                [chann, &out]() {
                    auto res = chann.impl->try_recv(out);
                    switch (res) {
                    case RecvStatus::Ok:
                        return SelectTryStatus::Ok;
                    case RecvStatus::Empty:
                        return SelectTryStatus::NotReady;
                    case RecvStatus::Closed:
                        out = std::nullopt;
                        return SelectTryStatus::Closed;
                    }
                    CR_UNREACHABLE();
                },
            .enqueue =
                [chann](SelectState* state) {
                    SelectWaiter waiter(state);
                    chann.impl->add_recv_waiter(waiter);
                },
            .dequeue = [chann](SelectState* state) { chann.impl->remove_waiters(state); }
        };
        branches.emplace_back(std::move(branch));
        return index;
    }

    std::optional<size_t> select();

private:
    Context& ctx;
    std::vector<SelectBranch> branches;
};
