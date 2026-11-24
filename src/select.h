#pragma once

#include "channel.h"

enum class SelectTryStatus {
    Ok,
    Again,
    Closed,
};

struct SelectBranch {
    size_t index;
    std::function<SelectTryStatus(void)> try_now;
    std::function<void(SelectState*)> enqueue;
    std::function<void(SelectState*)> dequeue;
};

class Selector {
public:
    Selector(Context& ctx);
    Selector(const Selector&) = delete;
    Selector& operator=(const Selector&) = delete;

    template <class T>
    size_t send(Sender<T> chann, const T& value) {
        return this->send(chann, T(value));
    }

    template <class T>
    size_t send(Sender<T> chann, T&& value) {
        CR_ASSERT(chann.impl->ctx == this->ctx);
        size_t index = branches.size();
        SelectBranch branch = SelectBranch{
            .index = index,
            .try_now =
                [chann, &value]() {
                    auto res = chann.impl->try_send(std::move(value));
                    switch (res) {
                    case SendStatus::Ok:
                        return SelectTryStatus::Ok;
                    case SendStatus::Full:
                        return SelectTryStatus::Again;
                    case SendStatus::Closed:
                        return SelectTryStatus::Closed;
                    }
                },
            .enqueue =
                [chann, index](SelectState* state) {
                    SelectWaiter waiter(state, index);
                    chann.impl->enqueue_sender(waiter);
                },
            .dequeue = [chann](SelectState* state) { chann.impl->dequeue(state); }
        };
        branches.emplace_back(std::move(branch));
        closed.push_back(false);
        return index;
    }

    template <class T>
    size_t recv(Receiver<T> chann, std::optional<T>& out) {
        CR_ASSERT(&chann.impl->ctx == &this->ctx);
        size_t index = branches.size();
        SelectBranch branch = SelectBranch{
            .index = index,
            .try_now =
                [chann, &out]() {
                    T tmp;
                    auto res = chann.impl->try_recv(tmp);
                    switch (res) {
                    case RecvStatus::Ok:
                        out = std::move(tmp);
                        return SelectTryStatus::Ok;
                    case RecvStatus::Empty:
                        return SelectTryStatus::Again;
                    case RecvStatus::Closed:
                        out = std::nullopt;
                        return SelectTryStatus::Closed;
                    }
                    CR_UNREACHABLE();
                },
            .enqueue =
                [chann, index](SelectState* state) {
                    SelectWaiter waiter(state, index);
                    chann.impl->enqueue_receiver(waiter);
                },
            .dequeue = [chann](SelectState* state) { chann.impl->dequeue(state); }
        };
        branches.emplace_back(std::move(branch));
        closed.push_back(false);
        return index;
    }

    size_t select();

private:
    Context& ctx;
    std::vector<SelectBranch> branches;
    std::vector<bool> closed;
};
