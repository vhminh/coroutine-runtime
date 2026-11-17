#pragma once

#include <cstddef>
#include <cstdint>

const size_t STACK_SIZE = 64 * 1024;

class Context;

struct Stack {
    Stack();
    Stack(const Stack&) = delete;
    Stack(Stack&&);
    ~Stack();

    std::byte* top() const;

private:
    std::byte* ptr;
};

struct Fiber {
public:
    Fiber(const Context& parent, uint64_t id, Stack&&, void (*func)(Context&));
    Fiber(const Fiber&) = delete;
    ~Fiber();
    const uint64_t id;

private:
    const Context& parent;
    const Stack stack;
    void (*func)(Context&);
};
