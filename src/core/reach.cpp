#include "reach.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

int nearest_within(const std::vector<Vec3> &points, Vec3 from, float reach) {
    int best = -1;
    float nearest = 0;
    for (size_t i = 0; i < points.size(); i++) {
        float d = std::hypot(points[i].x - from.x, points[i].y - from.y, points[i].z - from.z);
        if (d <= reach && (best < 0 || d < nearest)) nearest = d, best = (int)i;
    }
    return best;
}

Vec3 nearest_in_box(Vec3 p, Vec3 origin, float heading, Vec3 lo, Vec3 hi) {
    float c = std::cos(heading), s = std::sin(heading), dx = p.x - origin.x, dy = p.y - origin.y;
    // across and along the box as it lies before the turn
    float x = std::clamp(dx * c - dy * s, lo.x, hi.x), y = std::clamp(dx * s + dy * c, lo.y, hi.y);
    return {origin.x + x * c + y * s, origin.y - x * s + y * c, origin.z + std::clamp(p.z - origin.z, lo.z, hi.z)};
}

}
