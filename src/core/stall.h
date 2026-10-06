#pragma once
#include "frame.h"

namespace sm64nv {

// the stick held while mario stays put, reported once when it has gone on long enough
class StallWatch {
public:
    static constexpr int kTicks = 45;
    // sm64 units across the ground, height is ignored so jumping in place still counts
    static constexpr float kMove = 20;

    // true on the tick a stall is reported
    bool feed(Vec3 pos, float stick);

private:
    Vec3 anchor_{};
    int ticks_ = 0;
    bool reported_ = false;
};

}
