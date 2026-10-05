#pragma once

namespace sm64nv {

// sm64 physics run at 30 ticks a second whatever the game frame rate is
class FixedStep {
public:
    static constexpr int kMaxTicks = 4;
    static constexpr double kTick = 1.0 / 30;

    int advance(double seconds);
    double alpha() const { return acc_ / kTick; }

private:
    double acc_ = 0;
};

}
