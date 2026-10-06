#include "geo.h"

#include <utility>

namespace sm64nv {

Geo::Geo()
    : pos(9 * SM64_GEO_MAX_TRIANGLES), normal(9 * SM64_GEO_MAX_TRIANGLES), color(9 * SM64_GEO_MAX_TRIANGLES),
      uv(6 * SM64_GEO_MAX_TRIANGLES) {}

SM64MarioGeometryBuffers Geo::view() { return {pos.data(), normal.data(), color.data(), uv.data(), tris}; }

Vec3 lerp(Vec3 a, Vec3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

void blend_geo(const Geo &from, const Geo &to, float t, Geo &out) {
    out.color = to.color, out.uv = to.uv, out.tris = to.tris;
    if (from.tris != to.tris) {
        out.pos = to.pos, out.normal = to.normal;
        return;
    }
    for (size_t i = 0; i < 9u * to.tris; i++) {
        out.pos[i] = from.pos[i] + (to.pos[i] - from.pos[i]) * t;
        out.normal[i] = from.normal[i] + (to.normal[i] - from.normal[i]) * t;
    }
}

void MarioTicks::tick(int32_t id, const SM64MarioInputs &in, SM64MarioState &state) {
    std::swap(prev, cur);
    SM64MarioGeometryBuffers b = cur.view();
    sm64_mario_tick(id, &in, &state, &b);
    cur.tris = b.numTrianglesUsed;
    prev_pos = cur_pos;
    cur_pos = {state.position[0], state.position[1], state.position[2]};
    if (count++ == 0) prev = cur, prev_pos = cur_pos;
}

void MarioTicks::reset(Vec3 spawn) {
    count = 0, prev.tris = cur.tris = 0;
    prev_pos = cur_pos = spawn;
}

Vec3 MarioTicks::pos(float alpha) const { return lerp(prev_pos, cur_pos, alpha); }

void MarioTicks::draw(float alpha, Geo &out) const { blend_geo(prev, cur, alpha, out); }

}
