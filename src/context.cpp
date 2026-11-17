#include "context.h"

#include "fiber.h"

Context::Context() {}

const Fiber& Context::spawn(void (*func)(Context&)) {
    Stack stack; // TODO: pool stacks
    fibers.emplace_back(std::make_unique<Fiber>(*this, this->num_fibers++, std::move(stack), func));
    return *fibers.back().get();
}

void Context::run_loop() {
    while (!fibers.empty()) {
    }
}

void Context::yield() {}
