#pragma once
#include "frame.h"

#include <vector>

namespace sm64nv {

// index of the point closest to from and no further than reach, -1 when there is none
int nearest_within(const std::vector<Vec3> &points, Vec3 from, float reach);

}
