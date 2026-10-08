#pragma once
#include "frame.h"

#include <vector>

namespace sm64nv {

// index of the point closest to from and no further than reach, -1 when there is none
int nearest_within(const std::vector<Vec3> &points, Vec3 from, float reach);
// the spot nearest to p in a box around origin from lo to hi, turned by heading
// a wide door is placed by one end, and what counts is how near he stands to any of it
Vec3 nearest_in_box(Vec3 p, Vec3 origin, float heading, Vec3 lo, Vec3 hi);

Vec3 nearest_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c);

}
