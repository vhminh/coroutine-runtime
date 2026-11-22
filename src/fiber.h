#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

const size_t STACK_SIZE = 64 * 1024;

class Context;

class Stack {
public:
    Stack();
    Stack(const Stack&) = delete;
    Stack(Stack&&);
    ~Stack();

    std::byte* top() const;

private:
    std::byte* ptr;
};

enum class FiberState {
    Ready,
    Blocked,
    Finished,
};

typedef uint64_t FiberID;

struct Fiber {
public:
    Fiber(Context& parent, FiberID id, Stack&&, std::function<void(Context&)>);
    Fiber(const Fiber&) = delete;
    ~Fiber();
    const FiberID id;

    Context& parent;
    const Stack stack;
    void* sp = nullptr;
    FiberState state;
    void* resume_value = nullptr;
    std::function<void(Context&)> func;

private:
    void initialize_stack(); // platform specific
};
