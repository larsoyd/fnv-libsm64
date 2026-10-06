#include "reach.h"

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

}
