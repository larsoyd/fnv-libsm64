#include "surfaces.h"

#include <cmath>

namespace sm64nv {

static const uint16_t kTerrainStone = 1;

std::vector<Tri> quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { return {{a, b, c}, {a, c, d}}; }

std::vector<SM64Surface> build_surfaces(const Frame &f, const std::vector<Tri> &tris, SurfaceStats &stats) {
    std::vector<SM64Surface> out;
    out.reserve(tris.size());
    for (const Tri &t : tris) {
        SM64Surface s{0, 0, kTerrainStone, {}};
        const Vec3 v[3] = {to_sm64(f, t.a), to_sm64(f, t.b), to_sm64(f, t.c)};
        for (int i = 0; i < 3; i++) {
            s.vertices[i][0] = (int32_t)std::lround(v[i].x);
            s.vertices[i][1] = (int32_t)std::lround(v[i].y);
            s.vertices[i][2] = (int32_t)std::lround(v[i].z);
        }
        // same normal as the sm64 surface loader so the class matches what mario sees
        int64_t ax = s.vertices[1][0] - s.vertices[0][0], ay = s.vertices[1][1] - s.vertices[0][1],
                az = s.vertices[1][2] - s.vertices[0][2];
        int64_t bx = s.vertices[2][0] - s.vertices[1][0], by = s.vertices[2][1] - s.vertices[1][1],
                bz = s.vertices[2][2] - s.vertices[1][2];
        int64_t nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        if (!nx && !ny && !nz) {
            stats.degenerate++;
            continue;
        }
        double up = ny / std::sqrt(double(nx) * nx + double(ny) * ny + double(nz) * nz);
        if (up > 0.01) stats.floors++;
        else if (up < -0.01) stats.ceilings++;
        else stats.walls++;
        out.push_back(s);
    }
    return out;
}

}
