#pragma once
#include "libsm64.h"

namespace sm64nv {

struct Vec3 {
    float x, y, z;
};

// sm64 is y up, the game is z up, scale is sm64 units per game unit
struct Frame {
    Vec3 origin;
    float scale;
};

struct Buttons {
    bool a, b, z;
    bool operator==(const Buttons &) const = default;
};

inline Vec3 dir_to_sm64(Vec3 g) { return {g.x, g.z, -g.y}; }
inline Vec3 dir_to_game(Vec3 s) { return {s.x, -s.z, s.y}; }

inline Vec3 to_sm64(const Frame &f, Vec3 g) {
    Vec3 d = dir_to_sm64({g.x - f.origin.x, g.y - f.origin.y, g.z - f.origin.z});
    return {d.x * f.scale, d.y * f.scale, d.z * f.scale};
}

inline Vec3 to_game(const Frame &f, Vec3 s) {
    Vec3 d = dir_to_game({s.x / f.scale, s.y / f.scale, s.z / f.scale});
    return {d.x + f.origin.x, d.y + f.origin.y, d.z + f.origin.z};
}

float heading_from_sm64_yaw(float yaw);
// wrapped to plus or minus pi because libsm64 casts it straight to int16
float sm64_yaw_from_heading(float heading);
SM64MarioInputs make_inputs(float cam_heading, float right, float forward, Buttons buttons);

}
