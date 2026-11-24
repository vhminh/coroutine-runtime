#include "coroutine-runtime.h"

#include <chrono>
#include <initializer_list>
#include <iostream>
#include <optional>
using namespace std::chrono_literals;

Receiver<int> channel_from_list(Context& c, std::initializer_list<int> items) {
    auto [s, r] = make_channel<int>(c, items.size());
    for (int item : items) {
        s.send(item);
    }
    s.close();
    return r;
}

Receiver<int> after(Context& c, Context::Clock::duration interval, int val) {
    auto [s, r] = make_channel<int>(c, 1);
    c.spawn([=](Context& c) mutable {
        c.sleep(interval);
        s.send(val);
        s.close();
    });
    return r;
}

int main() {
    Context c;
    c.spawn([](Context& c) {
        auto chan0 = channel_from_list(c, {0, 0});
        auto chan1 = after(c, 1s, 1);
        auto chan2 = after(c, 2s, 2);
        auto chan3 = after(c, 3s, 3);
        std::optional<int> value;
        Selector selector(c);
        std::optional<size_t> selected;
        size_t selected0 = selector.recv(chan0, value);
        size_t selected1 = selector.recv(chan1, value);
        size_t selected2 = selector.recv(chan2, value);
        size_t selected3 = selector.recv(chan3, value);
        while ((selected = selector.select()) != std::nullopt) {
            if (selected == selected0) {
                if (value) {
                    std::cout << "channel 0 recv " << *value << std::endl;
                } else {
                    std::cout << "channel 0 closed" << std::endl;
                }
            } else if (selected == selected1) {
                if (value) {
                    std::cout << "channel 1 recv " << *value << std::endl;
                } else {
                    std::cout << "channel 1 closed" << std::endl;
                }
            } else if (selected == selected2) {
                if (value) {
                    std::cout << "channel 2 recv " << *value << std::endl;
                } else {
                    std::cout << "channel 2 closed" << std::endl;
                }
            } else if (selected == selected3) {
                if (value) {
                    std::cout << "channel 3 recv " << *value << std::endl;
                } else {
                    std::cout << "channel 3 closed" << std::endl;
                }
            } else {
                CR_UNREACHABLE("unexpected selected index {}", *selected);
            }
        }
    });
    c.run_loop();
    return 0;
}
