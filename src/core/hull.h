#pragma once
#include "surfaces.h"

#include <vector>

namespace sm64nv {

// outward wound triangles of the convex hull, empty when the points are flat
std::vector<Tri> convex_hull(const std::vector<Vec3> &pts);
std::vector<Tri> box_tris(Vec3 center, Vec3 half);

}
