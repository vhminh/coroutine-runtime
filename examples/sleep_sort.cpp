#include "coroutine-runtime.h"

#include <iostream>
using namespace std::chrono_literals;

int main() {
    std::vector<int> numbers = {6, 1, 5, 2, 8, 9, 3, 4, 7};
    Context c;
    for (int num : numbers) {
        c.spawn([num](Context& c) {
            c.sleep(num * 10ms);
            std::cout << num << std::endl;
        });
    }
    c.run_loop();
    return 0;
}
