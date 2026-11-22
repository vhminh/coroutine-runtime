#if defined(__aarch64__)

#include "context.h"
#include "fiber.h"

extern "C" void fiber_start(Fiber* f) {
    Context::run_fiber(*f);
}

extern "C" void fiber_start_stub();

void Fiber::initialize_stack() {
    auto* sp = reinterpret_cast<uintptr_t*>(this->stack.top() - 160);
    sp[0] = reinterpret_cast<uintptr_t>(this);              // x19
    sp[10] = 0;                                             // x29
    sp[11] = reinterpret_cast<uintptr_t>(fiber_start_stub); // x30
    this->sp = sp;
}

asm(R"(
    .pushsection .text
    .globl swap_context
swap_context:
    sub  sp, sp, #160
    stp  x19, x20, [sp, #0]
    stp  x21, x22, [sp, #16]
    stp  x23, x24, [sp, #32]
    stp  x25, x26, [sp, #48]
    stp  x27, x28, [sp, #64]
    stp  x29, x30, [sp, #80]
    stp  d8,  d9,  [sp, #96]
    stp  d10, d11, [sp, #112]
    stp  d12, d13, [sp, #128]
    stp  d14, d15, [sp, #144]
    mov  x3, sp
    str  x3, [x0]             // *save_sp = sp
    mov  sp, x1               // sp = load_sp
    ldp  x19, x20, [sp, #0]
    ldp  x21, x22, [sp, #16]
    ldp  x23, x24, [sp, #32]
    ldp  x25, x26, [sp, #48]
    ldp  x27, x28, [sp, #64]
    ldp  x29, x30, [sp, #80]
    ldp  d8,  d9,  [sp, #96]
    ldp  d10, d11, [sp, #112]
    ldp  d12, d13, [sp, #128]
    ldp  d14, d15, [sp, #144]
    add  sp, sp, #160
    mov  x0, x2               // return = ret_val
    ret

    .globl fiber_start_stub
fiber_start_stub:
    mov  x0, x19
    bl   fiber_start
    .popsection
)");

#endif
