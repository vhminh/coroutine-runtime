#include "coroutine-runtime.h"

#include <iostream>
using namespace std::chrono_literals;

const unsigned SPEED = 2;

int main() {
    Context c;
    c.spawn([](Context& c) {
        auto sem = c.new_semaphore();
        c.spawn([sem](Context& c) {
            for (int i = 1; i <= 3; ++i) {
                c.spawn([sem, i](Context& c) {
                    for (int j = 0; j < 2; ++j) {
                        std::cout << i << std::endl;
                        c.sleep(1000ms / SPEED);
                        c.release(sem);
                        if (j + 1 < 2) {
                            c.sleep(3000ms / SPEED);
                        }
                    }
                });
                c.sleep(1000ms / SPEED);
            }
        });
        for (int i = 0; i < 2; ++i) {
            c.acquire(sem);
            c.acquire(sem);
            c.acquire(sem);
            std::cout << "GO" << std::endl;
        }
        c.free_semaphore(sem);
    });
    c.run_loop();
}
