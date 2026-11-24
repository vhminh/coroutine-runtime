#pragma once

#include "check.h"
#include "context.h"
#include "def.h"
#include "waiter.h"

#include <deque>
#include <optional>
#include <utility>

enum class SendStatus { Ok, Full, Closed };
enum class RecvStatus { Ok, Empty, Closed };

template <class T> class Sender;
template <class T> class Receiver;

template <class T>
class ChanImpl {
public:
    friend class Selector;
    ChanImpl(Context& ctx, size_t capacity) : ctx(ctx), capacity(capacity) {}
    ChanImpl(const ChanImpl&) = delete;
    ChanImpl& operator=(const ChanImpl&) = delete;

    bool send(const T& value) { return send(T(value)); }

    bool send(T&& value) {
        CR_ASSERT(ctx.running, "must be running inside a fiber");
        if (this->is_closed) {
            return false;
        }
        while (!this->is_closed && this->full()) {
            auto& fiber = this->ctx.current();
            WakeToken token(fiber.id);
            this->send_waiters.push(&token);
            this->ctx.park(fiber);
        }
        if (this->is_closed) {
            // channel closed while we're blocking
            return false;
        }
        CR_ASSERT(this->buffer.size() < capacity);
        this->buffer.emplace_back(std::forward<T>(value));
        this->recv_waiters.wake_one(this->ctx);
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
        this->recv_waiters.wake_one(this->ctx);
        return SendStatus::Ok;
    }

    std::optional<T> recv() {
        CR_ASSERT(ctx.running, "must be running inside a fiber");
        if (this->is_closed && this->empty()) {
            return std::nullopt;
        }
        while (!this->is_closed && this->empty()) {
            auto& fiber = this->ctx.current();
            WakeToken token(fiber.id);
            this->recv_waiters.push(&token);
            this->ctx.park(fiber);
        }
        if (this->empty()) {
            // channel closed and drained while we're blocking
            return std::nullopt;
        }
        T out = std::move(this->buffer.front());
        this->buffer.pop_front();
        this->send_waiters.wake_one(this->ctx);
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
        this->send_waiters.wake_one(this->ctx);
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
        this->send_waiters.wake_one(this->ctx);
        return RecvStatus::Ok;
    }

    void close() {
        this->is_closed = true;
        this->send_waiters.wake_all(this->ctx);
        this->recv_waiters.wake_all(this->ctx);
    }

    bool closed() const { return this->is_closed; }

    bool empty() const { return this->buffer.empty(); }

    bool full() const { return this->buffer.size() >= capacity; }

    void add_recv_waiter(WakeToken* token) {
        CR_ASSERT(token);
        CR_ASSERT(this->empty());
        this->recv_waiters.push(token);
    }

    void remove_waiters(WakeToken* token) {
        CR_ASSERT(token);
        this->send_waiters.remove(token);
        this->recv_waiters.remove(token);
    }

private:
    Context& ctx;
    std::deque<T> buffer;
    const size_t capacity;
    bool is_closed = false;
    WaitQueue send_waiters;
    WaitQueue recv_waiters;
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
