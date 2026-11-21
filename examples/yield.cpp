#include "coroutine-runtime.h"

#include <format>
#include <iostream>

void counter(Context& c) {
    for (int i = 1; i <= 5; ++i) {
        std::cout << std::format("coroutine {}: {}", c.current_fiber_id(), i) << std::endl;
        c.yield();
    }
}

int main() {
    Context c;
    c.spawn(counter);
    c.spawn(counter);
    c.spawn(counter);
    c.run_loop();
    return 0;
}
