#include "coroutine-runtime.h"

#include <cassert>
#include <iostream>
using namespace std::chrono_literals;

const unsigned SPEED = 2;

int main() {
    Context c;
    c.spawn([](Context& c) {
        Semaphore sem(c);
        c.spawn([&sem](Context& c) {
            for (int i = 1; i <= 3; ++i) {
                c.spawn([&sem, i](Context& c) {
                    for (int j = 0; j < 2; ++j) {
                        std::cout << i << std::endl;
                        c.sleep(1000ms / SPEED);
                        sem.release();
                        if (j + 1 < 2) {
                            c.sleep(3000ms / SPEED);
                        }
                    }
                });
                c.sleep(1000ms / SPEED);
            }
        });
        for (int i = 0; i < 2; ++i) {
            assert(sem.try_acquire() == false);
            sem.acquire();
            assert(sem.try_acquire() == false);
            sem.acquire();
            assert(sem.try_acquire() == false);
            sem.acquire();
            std::cout << "GO" << std::endl;
        }
    });
    c.run_loop();
}
