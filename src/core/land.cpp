#include "land.h"

namespace sm64nv {

std::vector<Tri> land_tris(const Vec3 *points, int n, Vec3 offset, uint32_t owner) {
    auto at = [&](int i, int j) {
        const Vec3 &p = points[j * n + i];
        return Vec3{p.x + offset.x, p.y + offset.y, p.z + offset.z};
    };
    std::vector<Tri> out;
    for (int j = 0; j + 1 < n; j++)
        for (int i = 0; i + 1 < n; i++) {
            Vec3 a = at(i, j), b = at(i + 1, j), c = at(i + 1, j + 1), d = at(i, j + 1);
            out.push_back({a, b, c, owner, true});
            out.push_back({a, c, d, owner, true});
        }
    return out;
}

}
