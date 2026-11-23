#include "fiber.h"

#include "check.h"
#include "context.h"

#include <sys/mman.h>
#include <utility>

static size_t page_size() {
    static const size_t ps = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return ps;
}

static size_t alloc_size() {
    static const size_t sz = (STACK_SIZE + page_size() - 1) / page_size() * page_size() + page_size();
    return sz;
}

Stack::Stack() {
    void* p = mmap(
        nullptr, alloc_size(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_NORESERVE, -1, 0
    );
    if (p == MAP_FAILED) {
        throw SystemError(errno, "mmap");
    }

    // guard the lowest page, any access faults
    if (mprotect(p, page_size(), PROT_NONE) != 0) {
        munmap(p, alloc_size());
        throw SystemError(errno, "mprotect");
    }

    this->ptr = static_cast<std::byte*>(p);
}

Stack::Stack(Stack&& s) : ptr(s.ptr) {
    s.ptr = nullptr;
}

Stack::~Stack() {
    if (this->ptr) {
        munmap(this->ptr, alloc_size());
        this->ptr = nullptr;
    }
}

std::byte* Stack::top() const {
    return ptr + alloc_size();
}

Fiber::Fiber(Context& parent, FiberID id, Stack&& stack, std::function<void(Context&)> func) :
    id(id), parent(parent), stack(std::move(stack)), state(FiberState::Ready), func(func) {
    this->initialize_stack();
}

Fiber::~Fiber() {}
