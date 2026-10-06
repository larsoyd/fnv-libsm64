#include "arrival.h"

namespace sm64nv {

void ArrivalWatch::loaded_game() { pending_ = true, settled_ = 0; }

bool ArrivalWatch::frame(uintptr_t cell, bool blocked, bool taken) {
    settled_ = !cell ? 0 : cell == last_ ? settled_ + 1 : 1;
    last_ = cell;
    if (cell && cell != seen_) seen_ = cell, pending_ = true;
    if (!pending_ || settled_ < settle_ || (blocked && !taken)) return false;
    pending_ = false;
    return !taken;
}

}
