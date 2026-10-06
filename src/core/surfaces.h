#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

struct Tri {
    Vec3 a, b, c;
    uint32_t owner = 0;
    // part of a closed convex shape and wound to face out of it
    bool solid = false;
};

struct SurfaceStats {
    uint32_t floors = 0, walls = 0, ceilings = 0, degenerate = 0, stood_up = 0, still_steep = 0;
};

// two triangles whose normal follows (b - a) x (c - a)
std::vector<Tri> quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d);
// kept gets the index in tris of each surface it returns
std::vector<SM64Surface> build_surfaces(const Frame &f, const std::vector<Tri> &tris, SurfaceStats &stats,
                                        std::vector<uint32_t> *kept = nullptr);
// how high the surface is at a spot on the ground, false beside it or for an upright one
bool height_at(const SM64Surface &s, float x, float z, float &height);
// unit length in sm64 axes, zero for a triangle with no area
Vec3 surface_normal(const SM64Surface &s);
// moves the corners across the ground onto the upright plane through the highest one
// false when no rounding of them gets it within the 0.01 that sm64 calls a wall
bool stand_up(SM64Surface &s);

}
