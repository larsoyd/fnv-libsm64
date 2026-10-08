#pragma once
#include <vector>

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

// how long frames took, for the usual frame and the slow ones rather than the single worst
class FrameTimes {
public:
    void add(float ms) { ms_.push_back(ms); }
    int count() const { return (int)ms_.size(); }
    // the time q of all frames took at most, 1 is the slowest
    float quantile(float q) const;
    int over(float ms) const;

private:
    std::vector<float> ms_;
};

}
