#include "trail.h"

#include <cstdio>

namespace sm64nv {

void Trail::add(int tick, Vec3 pos, uint32_t action, float forward, float right, Buttons buttons) {
    steps_.push_back({tick, pos, action, forward, right, buttons});
    if (steps_.size() > kTicks) steps_.pop_front();
}

std::vector<std::string> Trail::lines() const {
    std::vector<std::string> out;
    for (size_t i = 0, j; i < steps_.size(); i = j) {
        const Step &a = steps_[i];
        for (j = i + 1; j < steps_.size() && steps_[j].action == a.action && steps_[j].buttons == a.buttons; j++) {}
        const Step &b = steps_[j - 1];
        char buf[200];
        snprintf(buf, sizeof buf, "ticks=%d-%d action=%08X from=%.1f,%.1f,%.1f to=%.1f,%.1f,%.1f forward=%.2f right=%.2f a=%d b=%d z=%d",
                 a.tick, b.tick, a.action, a.pos.x, a.pos.y, a.pos.z, b.pos.x, b.pos.y, b.pos.z, b.forward, b.right, b.buttons.a,
                 b.buttons.b, b.buttons.z);
        out.push_back(buf);
    }
    return out;
}

}
