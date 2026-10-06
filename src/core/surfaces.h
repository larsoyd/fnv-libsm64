#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

struct Tri {
    Vec3 a, b, c;
    uint32_t owner = 0;
};

struct SurfaceStats {
    uint32_t floors = 0, walls = 0, ceilings = 0, degenerate = 0, stood_up = 0, still_steep = 0;
};

// two triangles whose normal follows (b - a) x (c - a)
std::vector<Tri> quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d);
// owners gets the owner of each kept triangle in the same order
std::vector<SM64Surface> build_surfaces(const Frame &f, const std::vector<Tri> &tris, SurfaceStats &stats,
                                        std::vector<uint32_t> *owners = nullptr);
// unit length in sm64 axes, zero for a triangle with no area
Vec3 surface_normal(const SM64Surface &s);
// moves the corners across the ground onto the upright plane through the middle
// false when no rounding of them gets it within the 0.01 that sm64 calls a wall
bool stand_up(SM64Surface &s);

}
