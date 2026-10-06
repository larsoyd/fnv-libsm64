#pragma once
#include "core/frame.h"

#include <cstdint>

namespace sm64nv {

// directinput scan codes
enum : uint8_t {
    kKeyW = 0x11,
    kKeyE = 0x12,
    kKeyA = 0x1E,
    kKeyS = 0x1F,
    kKeyD = 0x20,
    kKeySpace = 0x39,
    kKeyLShift = 0x2A,
    kKeyRShift = 0x36,
    kKeyLCtrl = 0x1D,
    kKeyRCtrl = 0x9D,
    kKeyM = 0x32,
};

struct Pad {
    float right, forward;
    Buttons buttons;
    bool operator==(const Pad &) const = default;
};

enum : uint16_t { kPadDpadDown = 0x0002, kPadA = 0x1000, kPadX = 0x4000, kPadY = 0x8000 };

// xinput gamepad report as the game keeps it
struct GamepadState {
    uint16_t buttons;
    uint8_t left_trigger, right_trigger;
    int16_t lx, ly, rx, ry;
};
static_assert(sizeof(GamepadState) == 12);

// keys and mouse are the game's raw state arrays, high bit set means down
Pad read_pad(const uint8_t *keys, const uint8_t *mouse);
Pad read_gamepad(const GamepadState &g);
// a stick out of its deadzone wins over the keys, buttons from either count
Pad merge_pads(const Pad &keys, const Pad &pad);
// m on the keyboard or dpad down on the gamepad, which the game leaves unbound
bool toggle_held(const uint8_t *keys, const GamepadState &pad);

// e on the keyboard or y on the gamepad
bool activate_held(const uint8_t *keys, const GamepadState &pad);

struct Press {
    bool held = false;
    bool edge(bool down);
};

}
