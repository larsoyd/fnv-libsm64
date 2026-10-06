#include "pad.h"

#include <cmath>

namespace sm64nv {

static bool down(const uint8_t *keys, uint8_t k) { return keys[k] & 0x80; }

Pad read_pad(const uint8_t *keys, const uint8_t *mouse) {
    float right = (float)down(keys, kKeyD) - down(keys, kKeyA);
    float forward = (float)down(keys, kKeyW) - down(keys, kKeyS);
    float len = std::hypot(right, forward);
    if (len > 1) right /= len, forward /= len;
    Buttons b{down(keys, kKeySpace), down(keys, kKeyLShift) || down(keys, kKeyRShift) || (mouse[0] & 0x80),
              down(keys, kKeyLCtrl) || down(keys, kKeyRCtrl)};
    return {right, forward, b};
}

}
