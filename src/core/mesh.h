#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

inline constexpr float kAmbient = 0.55f;

// fixed size arrays so the engine buffers never resize, rgba colors and uv pairs are flat
struct MeshOut {
    std::vector<Vec3> pos, normal;
    std::vector<float> color, uv;
    uint32_t tris = 0;
};

void convert_mesh(const Frame &f, const SM64MarioGeometryBuffers &g, Vec3 anchor, Vec3 to_light, MeshOut &out);

}
