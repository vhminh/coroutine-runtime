#include "coroutine-runtime.h"

#include <format>
#include <iostream>

void even(Context& c) {
    for (int i = 2; i <= 4; i += 2) {
        std::cout << std::format("coroutine {}: {}", c.current_fiber_id(), i) << std::endl;
        c.yield();
    }
}

void odd(Context& c) {
    for (int i = 1; i <= 3; i += 2) {
        std::cout << std::format("coroutine {}: {}", c.current_fiber_id(), i) << std::endl;
        c.yield();
    }
}

void counter(Context& c) {
    std::cout << "spawning `odd` coroutine" << std::endl;
    c.spawn(odd);
    std::cout << "spawning `even` coroutine" << std::endl;
    c.spawn(even);
}

void fiber_main(Context& c) {
    c.spawn(counter);
    c.spawn(counter);
}

int main() {
    Context c;
    c.spawn(fiber_main);
    c.run_loop();
    return 0;
}
