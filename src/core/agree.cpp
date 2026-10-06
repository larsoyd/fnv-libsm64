#include "agree.h"

namespace sm64nv {

bool Agreement::add(float err) {
    samples++, off += err > limit;
    if (err <= worst) return false;
    worst = err;
    return true;
}

bool Agreement::holds() const { return samples && off * kShare <= samples; }

}
