#pragma once
#include "frame.h"

namespace sm64nv {

// when the world is gathered again around mario
class Regather {
public:
    static constexpr int kRetryTicks = 15;

    explicit Regather(float move) : move_(move) {}
    // how far he is from the middle of what is loaded
    float moved(Vec3 at) const;
    bool due(Vec3 at, int tick) const;
    void loaded(Vec3 center);
    // what was loaded before stays, so its middle does too
    void refused(int tick);

private:
    float move_;
    Vec3 center_{};
    int wait_until_ = 0;
};

}
