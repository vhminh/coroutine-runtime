#if defined(__x86_64__)

#include "context.h"
#include "fiber.h"

extern "C" void fiber_start(Fiber* f) {
    f->func(f->parent);
    f->state = FiberState::Finish;
    swap_context(&f->sp, f->parent.sched_sp);
}
void __attribute__((naked)) fiber_start_stub() {
    // move the arg from r12 to rdi and call `fiber_start`
    asm("movq %r12, %rdi\n"
        "call fiber_start\n"
        "ud2\n");
}

void Fiber::initialize_stack() {
    auto* sp = reinterpret_cast<uintptr_t*>(this->stack.top());
    *--sp = reinterpret_cast<uintptr_t>(
        fiber_start_stub
    );                                         // the first `ret` goes to `fiber_start`, with the arg stored in r12
    *--sp = 0;                                 // rbp
    *--sp = 0;                                 // rbx
    *--sp = reinterpret_cast<uintptr_t>(this); // r12 = this
    *--sp = 0;                                 // r13
    *--sp = 0;                                 // r14
    *--sp = 0;                                 // r15
    this->sp = sp;
}

void __attribute__((naked)) swap_context(void** save_sp, void* load_sp) {
    asm("pushq   %rbp\n"
        "pushq   %rbx\n"
        "pushq   %r12\n"
        "pushq   %r13\n"
        "pushq   %r14\n"
        "pushq   %r15\n"
        "movq    %rsp, (%rdi) # *save_sp = rsp\n"
        "movq    %rsi, %rsp   # rsp = load_sp\n"
        "popq    %r15\n"
        "popq    %r14\n"
        "popq    %r13\n"
        "popq    %r12\n"
        "popq    %rbx\n"
        "popq    %rbp\n"
        "ret\n");
}

#endif
