#include "frame.h"

#include <cmath>

namespace sm64nv {

static const float kTau = 6.28318531f;

float heading_from_sm64_yaw(float yaw) {
    float h = std::fmod(3.14159265f - yaw, kTau);
    h = h < 0 ? h + kTau : h;
    return h >= kTau - 1e-6f ? 0 : h;
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
