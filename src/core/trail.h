#pragma once
#include "frame.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace sm64nv {

// what mario did over the last few seconds, kept for the log when something goes wrong
class Trail {
public:
    static constexpr int kTicks = 150;
    void add(int tick, Vec3 pos, uint32_t action, float forward, float right, Buttons buttons);
    // oldest first, ticks in a row with one action and the same buttons share a line
    std::vector<std::string> lines() const;
    void clear() { steps_.clear(); }

private:
    struct Step {
        int tick;
        Vec3 pos;
        uint32_t action;
        float forward, right;
        Buttons buttons;
    };
    std::deque<Step> steps_;
};

}
