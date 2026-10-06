#include "arrival.h"

namespace sm64nv {

void ArrivalWatch::again() { pending_ = true, settled_ = 0; }

bool ArrivalWatch::frame(uintptr_t place, bool blocked, bool taken) {
    settled_ = !place ? 0 : place == last_ ? settled_ + 1 : 1;
    last_ = place;
    if (place && place != seen_) seen_ = place, pending_ = true;
    if (!pending_ || settled_ < settle_ || (blocked && !taken)) return false;
    pending_ = false;
    return !taken;
}

}
