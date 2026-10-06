#include "stall.h"

#include <cmath>

namespace sm64nv {

bool StallWatch::feed(Vec3 pos, float stick) {
    bool held = stick >= 0.5f, moved = std::hypot(pos.x - anchor_.x, pos.z - anchor_.z) > kMove;
    if (!held || moved || !ticks_) {
        anchor_ = pos, reported_ = false;
        ticks_ = held;
        return false;
    }
    if (++ticks_ < kTicks || reported_) return false;
    return reported_ = true;
}

}
