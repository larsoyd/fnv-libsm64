#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

inline constexpr float kAmbient = 0.55f;
inline constexpr int kAtlasWidth = 1024;

// fixed size arrays so the engine buffers never resize, rgba colors and uv pairs are flat
struct MeshOut {
    std::vector<Vec3> pos, normal;
    std::vector<float> color, uv;
    uint32_t tris = 0;
};

// every vertex back at the middle with no color, and no triangles
void clear_mesh(MeshOut &out);

// how a shape sits under its parent node, rotation as three rows
struct Placing {
    float rot[9];
    Vec3 at;
    float scale;
};

// the placing that draws a shape built in world axes around world, whatever its parent's own
Placing place_under(const float parent_rot[9], Vec3 parent_at, float parent_scale, Vec3 world);
void convert_mesh(const Frame &f, const SM64MarioGeometryBuffers &g, Vec3 anchor, Vec3 to_light, MeshOut &out);
// only the textured triangles, lit grey so the atlas gives the color
void convert_decal(const Frame &f, const SM64MarioGeometryBuffers &g, Vec3 anchor, Vec3 to_light, MeshOut &out);

}
