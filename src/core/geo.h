#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

// one tick of libsm64 geometry in its own flat layout
struct Geo {
    std::vector<float> pos, normal, color, uv;
    uint16_t tris = 0;
    Geo();
    SM64MarioGeometryBuffers view();
};

Vec3 lerp(Vec3 a, Vec3 b, float t);
// a changed triangle count means different meshes so the newer tick wins outright
void blend_geo(const Geo &from, const Geo &to, float t, Geo &out);

// the last two ticks of one mario, frames between ticks draw a blend of both
struct MarioTicks {
    Geo prev, cur;
    Vec3 prev_pos{}, cur_pos{};
    int count = 0;
    void tick(int32_t id, const SM64MarioInputs &in, SM64MarioState &state);
    void reset(Vec3 spawn);
    // moves him after a tick, as if that tick had ended there
    void put_back(int32_t id, Vec3 pos, SM64MarioState &state);
    Vec3 pos(float alpha) const;
    void draw(float alpha, Geo &out) const;
};

}
