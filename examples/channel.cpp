#include "coroutine-runtime.h"

#include <cassert>
#include <iostream>
#include <optional>
using namespace std;
using namespace std::chrono_literals;

const unsigned PRODUCER_SPEED = 6;
const unsigned CONSUMER_SPEED = 3;

int main() {
    Context c;
    c.spawn([](Context& c) {
        auto [producer, consumer] = make_channel<int>(c, 2);
        c.spawn([&producer](Context& c) {
            for (int i = 0; i < 6; ++i) {
                bool ok = producer.send(i);
                assert(ok);
                cout << "sent " << i << endl;
                c.sleep(1000ms / PRODUCER_SPEED);
            }
            producer.close();
        });
        optional<int> tmp;
        while ((tmp = consumer.recv()) != nullopt) {
            cout << "received " << *tmp << endl;
            c.sleep(1000ms / CONSUMER_SPEED);
        }
    });
    c.run_loop();
    return 0;
}
