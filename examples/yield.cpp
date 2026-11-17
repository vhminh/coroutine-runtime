#include "coroutine-runtime.h"

#include <iostream>

void fiber_main(Context& c) {
    for (int i = 1; i <= 9; ++i) {
        std::cout << i << std::endl;
        c.yield();
    }
}

int main() {
    Context c;
    c.spawn(fiber_main);
    c.spawn(fiber_main);
    c.spawn(fiber_main);
    c.run_loop();
    return 0;
}
