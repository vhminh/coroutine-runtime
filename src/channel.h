#pragma once

#include "check.h"
#include "context.h"
#include "def.h"

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
    ChanImpl(Context& ctx, size_t capacity) : ctx(ctx), capacity(capacity) {}
    ChanImpl(const ChanImpl&) = delete;
    ChanImpl& operator=(const ChanImpl&) = delete;

    bool send(const T& value) { return send(T(value)); }

    bool send(T&& value) {
        CR_ASSERT(ctx.running, "must be running inside a fiber");
        if (this->is_closed) {
            return false;
        }
        if (this->buffer.size() >= this->capacity) {
            auto& fiber = this->ctx.current();
            full_waiters.push_back(fiber.id);
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
        if (this->buffer.size() >= this->capacity) {
            return SendStatus::Full;
        }
        this->buffer.emplace_back(std::forward<T>(value));
        this->notify_item_added();
        return SendStatus::Ok;
    }

    std::optional<T> recv() {
        CR_ASSERT(ctx.running, "must be running inside a fiber");
        if (this->is_closed && this->buffer.empty()) {
            return std::nullopt;
        }
        if (this->buffer.empty()) {
            auto& fiber = this->ctx.current();
            empty_waiters.push_back(fiber.id);
            this->ctx.park(fiber);
        }
        if (this->is_closed && this->buffer.empty()) {
            // channel closed while we're blocking
            return std::nullopt;
        }
        CR_ASSERT(this->buffer.size());
        T out = std::move(this->buffer.front());
        this->buffer.pop_front();
        this->notify_item_removed();
        return out;
    }

    [[nodiscard]] RecvStatus try_recv(T& out) {
        if (this->is_closed && this->buffer.empty()) {
            return RecvStatus::Closed;
        }
        if (this->buffer.empty()) {
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

private:
    Context& ctx;
    std::deque<T> buffer;
    const size_t capacity;
    bool is_closed = false;
    std::deque<FiberID> full_waiters;
    std::deque<FiberID> empty_waiters;

    void notify_item_added() {
        if (this->empty_waiters.size()) {
            auto fiberID = this->empty_waiters.front();
            this->empty_waiters.pop_front();
            this->ctx.unpark(*this->ctx.fibers.at(fiberID));
        }
    }
    void notify_item_removed() {
        if (this->full_waiters.size()) {
            auto fiberID = this->full_waiters.front();
            this->full_waiters.pop_front();
            this->ctx.unpark(*this->ctx.fibers.at(fiberID));
        }
    }
    void notify_all() {
        while (this->empty_waiters.size()) {
            auto fiberID = this->empty_waiters.front();
            this->empty_waiters.pop_front();
            this->ctx.unpark(*this->ctx.fibers.at(fiberID));
        }
        while (this->full_waiters.size()) {
            auto fiberID = this->full_waiters.front();
            this->full_waiters.pop_front();
            this->ctx.unpark(*this->ctx.fibers.at(fiberID));
        }
    }
};

template <class T>
class Sender {
    template <class T2>
    friend std::pair<Sender<T2>, Receiver<T2>> make_channel(Context& c, size_t capacity);

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
    auto impl = std::make_shared<ChanImpl<T>>(c, capacity);
    return std::make_pair(Sender<T>(impl), Receiver<T>(impl));
}
