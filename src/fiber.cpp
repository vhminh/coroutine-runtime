#include "fiber.h"

#include "context.h"

#include <cassert>
#include <sys/mman.h>
#include <utility>

Stack::Stack() {
    this->ptr = new std::byte[STACK_SIZE];
    assert(this->ptr);
    // this->ptr = mmap(nullptr, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    // assert(this->ptr != MAP_FAILED);
}

Stack::Stack(Stack&& s) : ptr(s.ptr) {
    s.ptr = nullptr;
}

Stack::~Stack() {
    if (ptr) {
        delete[] ptr;
        // munmap(ptr, STACK_SIZE);
        ptr = nullptr;
    }
}

std::byte* Stack::top() const {
    return ptr + STACK_SIZE;
}

Fiber::Fiber(Context& parent, FiberID id, Stack&& stack, void (*func)(Context&)) :
    id(id), parent(parent), stack(std::move(stack)), state(FiberState::Ready), func(func) {
    this->initialize_stack();
}

Fiber::~Fiber() {}
