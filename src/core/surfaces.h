#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

struct Tri {
    Vec3 a, b, c;
};

struct SurfaceStats {
    uint32_t floors = 0, walls = 0, ceilings = 0, degenerate = 0;
};

// two triangles whose normal follows (b - a) x (c - a)
std::vector<Tri> quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d);
std::vector<SM64Surface> build_surfaces(const Frame &f, const std::vector<Tri> &tris, SurfaceStats &stats);

}
