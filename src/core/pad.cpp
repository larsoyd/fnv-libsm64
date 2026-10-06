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

static const float kStickDeadzone = 7849, kStickMax = 32767;
static const uint8_t kTriggerThreshold = 30;

Pad read_gamepad(const GamepadState &g) {
    float x = g.lx, y = g.ly, mag = std::hypot(x, y);
    Pad p{};
    if (mag > kStickDeadzone) {
        float scale = std::fmin(1.0f, (mag - kStickDeadzone) / (kStickMax - kStickDeadzone)) / mag;
        p.right = x * scale, p.forward = y * scale;
    }
    p.buttons = {(g.buttons & kPadA) != 0, (g.buttons & kPadX) != 0,
                 g.left_trigger > kTriggerThreshold || g.right_trigger > kTriggerThreshold};
    return p;
}

Pad merge_pads(const Pad &keys, const Pad &pad) {
    Pad m = pad.right || pad.forward ? pad : keys;
    m.buttons = {keys.buttons.a || pad.buttons.a, keys.buttons.b || pad.buttons.b, keys.buttons.z || pad.buttons.z};
    return m;
}

bool toggle_held(const uint8_t *keys) { return down(keys, kKeyM); }

bool Press::edge(bool now) {
    bool fired = now && !held;
    held = now;
    return fired;
}

}
