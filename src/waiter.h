#pragma once
#include "fiber.h"

#include <deque>

struct WakeToken {
    WakeToken(FiberID);
    FiberID fiber_id;
    bool claimed = false;
};

class WaitQueue {
public:
    void push(WakeToken*);
    void remove(WakeToken*);
    void wake_one(Context&);
    void wake_all(Context&);
    bool empty();

private:
    std::deque<WakeToken*> q;
};
