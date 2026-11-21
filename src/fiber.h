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

enum FiberState {
    Ready,
    Finish,
};

typedef uint64_t FiberID;

struct Fiber {
public:
    Fiber(Context& parent, FiberID id, Stack&&, void (*func)(Context&));
    Fiber(const Fiber&) = delete;
    ~Fiber();
    const FiberID id;

    Context& parent;
    const Stack stack;
    void* sp = nullptr;
    FiberState state;
    void (*func)(Context&);

private:
    void initialize_stack(); // platform specific
};
