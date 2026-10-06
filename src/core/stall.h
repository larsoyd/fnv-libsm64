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

// where mario last had room to stand, to put him back from somewhere too low
class LastFit {
public:
    // sm64 units, less free height than this and no ground step or landing would have let him in
    static constexpr float kHeight = 160;
    // how many ticks in a row he has been too low with a spot to go back to, 0 when he fits
    int feed(Vec3 pos, float room);
    Vec3 pos() const { return pos_; }
    int low() const { return low_; }

private:
    Vec3 pos_{};
    bool known_ = false;
    int low_ = 0;
};

}
