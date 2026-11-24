#pragma once

#include "check.h"
#include "context.h"
#include "def.h"

#include <deque>
#include <optional>
#include <utility>
#include <variant>

enum class SendStatus { Ok, Full, Closed };
enum class RecvStatus { Ok, Empty, Closed };

template <class T> class Sender;
template <class T> class Receiver;


struct PlainWaiter {
    PlainWaiter(FiberID fiber_id);
    FiberID fiber_id;
};

struct SelectState {
    SelectState(FiberID);
    FiberID fiber_id;
    bool claimed = false;
};

struct SelectWaiter {
    SelectWaiter(SelectState* sel);
    SelectState* state;
};

struct Waiter {
public:
    Waiter(PlainWaiter w) : inner(w) {}
    Waiter(SelectWaiter w) : inner(w) {}
    std::variant<PlainWaiter, SelectWaiter> inner;

    FiberID fiber_id() const;
    bool stale() const;
    std::optional<FiberID> claim();
};

template <class T>
class ChanImpl {
public:
    friend class Selector;
    ChanImpl(Context& ctx, size_t capacity) : ctx(ctx), capacity(capacity) {}
    ChanImpl(const ChanImpl&) = delete;
    ChanImpl& operator=(const ChanImpl&) = delete;

    bool send(const T& value) { return send(T(value), PlainWaiter(this->ctx.current_fiber_id())); }

    bool send(const T& value, Waiter w) { return send(T(value), w); }

    bool send(T&& value) { return send(std::move(value), PlainWaiter(this->ctx.current_fiber_id())); }

    bool send(T&& value, Waiter w) {
        CR_ASSERT(ctx.running, "must be running inside a fiber");
        CR_ASSERT(ctx.current_fiber_id() == w.fiber_id());
        if (this->is_closed) {
            return false;
        }
        while (!this->is_closed && this->full()) {
            auto& fiber = this->ctx.current();
            this->send_waiters.push_back(w);
            this->ctx.park(fiber);
        }
        if (this->is_closed) {
            // channel closed while we're blocking
            return false;
        }
        CR_ASSERT(this->buffer.size() < capacity);
        this->buffer.emplace_back(std::forward<T>(value));
        this->notify_item_added();
        return true;
    }

    [[nodiscard]] SendStatus try_send(const T& value) { return try_send(T(value)); }

    [[nodiscard]] SendStatus try_send(T&& value) {
        if (this->is_closed) {
            return SendStatus::Closed;
        }
        if (this->full()) {
            return SendStatus::Full;
        }
        this->buffer.emplace_back(std::forward<T>(value));
        this->notify_item_added();
        return SendStatus::Ok;
    }

    std::optional<T> recv() { return recv(PlainWaiter(this->ctx.current_fiber_id())); }

    std::optional<T> recv(Waiter w) {
        CR_ASSERT(ctx.running, "must be running inside a fiber");
        CR_ASSERT(ctx.current_fiber_id() == w.fiber_id());
        if (this->is_closed && this->empty()) {
            return std::nullopt;
        }
        while (!this->is_closed && this->empty()) {
            auto& fiber = this->ctx.current();
            this->recv_waiters.push_back(w);
            this->ctx.park(fiber);
        }
        if (this->empty()) {
            // channel closed and drained while we're blocking
            return std::nullopt;
        }
        T out = std::move(this->buffer.front());
        this->buffer.pop_front();
        this->notify_item_removed();
        return out;
    }

    [[nodiscard]] RecvStatus try_recv(T& out) {
        if (this->is_closed && this->empty()) {
            return RecvStatus::Closed;
        }
        if (this->empty()) {
            return RecvStatus::Empty;
        }
        out = std::move(this->buffer.front());
        this->buffer.pop_front();
        this->notify_item_removed();
        return RecvStatus::Ok;
    }

    [[nodiscard]] RecvStatus try_recv(std::optional<T>& out) {
        if (this->is_closed && this->empty()) {
            out = std::nullopt;
            return RecvStatus::Closed;
        }
        if (this->empty()) {
            out = std::nullopt;
            return RecvStatus::Empty;
        }
        out = std::move(this->buffer.front());
        this->buffer.pop_front();
        this->notify_item_removed();
        return RecvStatus::Ok;
    }

    void close() {
        this->is_closed = true;
        this->notify_all();
    }

    bool closed() const { return this->is_closed; }

    bool empty() const { return this->buffer.empty(); }

    bool full() const { return this->buffer.size() >= capacity; }

    void add_recv_waiter(Waiter w) {
        CR_ASSERT(this->empty());
        this->recv_waiters.push_back(w);
    }

    // FIXME: this runs in O(n)
    void remove_waiters(SelectState* s) {
        auto mine = [&](const Waiter& w) {
            auto* p = std::get_if<SelectWaiter>(&w.inner);
            return p && p->state == s;
        };
        std::erase_if(this->send_waiters, mine);
        std::erase_if(this->recv_waiters, mine);
    }

private:
    Context& ctx;
    std::deque<T> buffer;
    const size_t capacity;
    bool is_closed = false;
    std::deque<Waiter> send_waiters;
    std::deque<Waiter> recv_waiters;

    void notify_item_added() {
        while (this->recv_waiters.size() && this->recv_waiters.front().stale()) {
            this->recv_waiters.pop_front();
        }
        if (this->recv_waiters.size()) {
            auto waiter = this->recv_waiters.front();
            this->recv_waiters.pop_front();
            auto fiber_id = waiter.claim();
            CR_ASSERT(fiber_id, "expect non stale waiter");
            this->ctx.unpark(*this->ctx.fibers.at(*fiber_id));
        }
    }
    void notify_item_removed() {
        while (this->send_waiters.size() && this->send_waiters.front().stale()) {
            this->send_waiters.pop_front();
        }
        if (this->send_waiters.size()) {
            auto waiter = this->send_waiters.front();
            this->send_waiters.pop_front();
            auto fiber_id = waiter.claim();
            CR_ASSERT(fiber_id, "expect non stale waiter");
            this->ctx.unpark(*this->ctx.fibers.at(*fiber_id));
        }
    }
    void notify_all() {
        while (this->recv_waiters.size()) {
            auto waiter = this->recv_waiters.front();
            this->recv_waiters.pop_front();
            auto fiber_id = waiter.claim();
            if (fiber_id) {
                this->ctx.unpark(*this->ctx.fibers.at(*fiber_id));
            }
        }
        while (this->send_waiters.size()) {
            auto waiter = this->send_waiters.front();
            this->send_waiters.pop_front();
            auto fiber_id = waiter.claim();
            if (fiber_id) {
                this->ctx.unpark(*this->ctx.fibers.at(*fiber_id));
            }
        }
    }
};

template <class T>
class Sender {
    template <class T2>
    friend std::pair<Sender<T2>, Receiver<T2>> make_channel(Context& c, size_t capacity);
    friend class Selector;

public:
    bool send(const T& value) { return impl->send(value); }
    bool send(T&& value) { return impl->send(std::move(value)); }
    [[nodiscard]] SendStatus try_send(const T& value) { return impl->try_send(value); }
    [[nodiscard]] SendStatus try_send(T&& value) { return impl->try_send(std::move(value)); }
    void close() { impl->close(); }
    bool closed() const { return impl->closed(); }

private:
    explicit Sender(Arc<ChanImpl<T>> impl) : impl(std::move(impl)) {}

    Arc<ChanImpl<T>> impl;
};

template <class T>
class Receiver {
    template <class T2>
    friend std::pair<Sender<T2>, Receiver<T2>> make_channel(Context& c, size_t capacity);
    friend class Selector;

public:
    std::optional<T> recv() { return impl->recv(); }
    [[nodiscard]] RecvStatus try_recv(T& out) { return impl->try_recv(out); }
    bool closed() const { return impl->closed(); }

private:
    explicit Receiver(Arc<ChanImpl<T>> impl) : impl(std::move(impl)) {}

    Arc<ChanImpl<T>> impl;
};

template <class T>
std::pair<Sender<T>, Receiver<T>> make_channel(Context& c, size_t capacity) {
    CR_ASSERT(capacity > 0, "unbuffered channel is not supported");
    auto impl = std::make_shared<ChanImpl<T>>(c, capacity);
    return std::make_pair(Sender<T>(impl), Receiver<T>(impl));
}
