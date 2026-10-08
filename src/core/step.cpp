#include "step.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

int FixedStep::advance(double seconds) {
    if (seconds > 0) acc_ += seconds;
    int n = 0;
    while (acc_ >= kTick && n < kMaxTicks) acc_ -= kTick, n++;
    if (acc_ >= kTick) acc_ = 0;
    return n;
}

float FrameTimes::quantile(float q) const {
    if (ms_.empty()) return 0;
    std::vector<float> sorted = ms_;
    std::ranges::sort(sorted);
    size_t at = (size_t)std::ceil(q * sorted.size());
    return sorted[std::clamp<size_t>(at, 1, sorted.size()) - 1];
}

int FrameTimes::over(float ms) const { return (int)std::ranges::count_if(ms_, [&](float f) { return f > ms; }); }

}
