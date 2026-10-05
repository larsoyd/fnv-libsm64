#include "step.h"

namespace sm64nv {

int FixedStep::advance(double seconds) {
    if (seconds > 0) acc_ += seconds;
    int n = 0;
    while (acc_ >= kTick && n < kMaxTicks) acc_ -= kTick, n++;
    if (acc_ >= kTick) acc_ = 0;
    return n;
}

}
