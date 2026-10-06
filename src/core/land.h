#pragma once
#include "surfaces.h"

#include <vector>

namespace sm64nv {

// terrain as n by n points in rows along y, two upward triangles per square
std::vector<Tri> land_tris(const Vec3 *points, int n, Vec3 offset, uint32_t owner);

}
