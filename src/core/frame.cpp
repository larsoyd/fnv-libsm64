#include "frame.h"

#include <cmath>

namespace sm64nv {

static const float kTau = 6.28318531f;

float heading_from_sm64_yaw(float yaw) {
    float h = std::fmod(3.14159265f - yaw, kTau);
    h = h < 0 ? h + kTau : h;
    return h >= kTau - 1e-6f ? 0 : h;
}

float sm64_yaw_from_heading(float heading) {
    // libsm64 scales by its own 3.14159 so stay just inside that before the int16 cast
    float y = std::remainder(3.14159265f - heading, kTau);
    return std::fmax(-3.14158f, std::fmin(3.14158f, y));
}

SM64MarioInputs make_inputs(float cam_heading, float right, float forward, Buttons buttons) {
    SM64MarioInputs in{};
    in.camLookX = std::sin(cam_heading);
    in.camLookZ = -std::cos(cam_heading);
    in.stickX = right;
    // libsm64 follows sdl where a positive y axis means down
    in.stickY = -forward;
    in.buttonA = buttons.a;
    in.buttonB = buttons.b;
    in.buttonZ = buttons.z;
    return in;
}

}
