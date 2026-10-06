#pragma once
#include "core/frame.h"

#include <cstdint>

namespace sm64nv {

// directinput scan codes
enum : uint8_t {
    kKeyW = 0x11,
    kKeyA = 0x1E,
    kKeyS = 0x1F,
    kKeyD = 0x20,
    kKeySpace = 0x39,
    kKeyLShift = 0x2A,
    kKeyRShift = 0x36,
    kKeyLCtrl = 0x1D,
    kKeyRCtrl = 0x9D,
};

struct Pad {
    float right, forward;
    Buttons buttons;
    bool operator==(const Pad &) const = default;
};

// keys and mouse are the game's raw state arrays, high bit set means down
Pad read_pad(const uint8_t *keys, const uint8_t *mouse);

}
