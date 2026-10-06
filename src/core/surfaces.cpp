#include "surfaces.h"

#include <cmath>

namespace sm64nv {

static const uint16_t kTerrainStone = 1;

std::vector<Tri> quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { return {{a, b, c}, {a, c, d}}; }

std::vector<SM64Surface> build_surfaces(const Frame &f, const std::vector<Tri> &tris, SurfaceStats &stats,
                                        std::vector<uint32_t> *owners) {
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
        Vec3 n = surface_normal(s);
        if (!n.x && !n.y && !n.z) {
            stats.degenerate++;
            continue;
        }
        if (n.y > 0.01f) stats.floors++;
        else if (n.y < -0.01f) stats.ceilings++;
        else stats.walls++;
        out.push_back(s);
        if (owners) owners->push_back(t.owner);
    }
    return out;
}

// same cross product as the sm64 surface loader so the class matches what mario sees
Vec3 surface_normal(const SM64Surface &s) {
    const auto &v = s.vertices;
    int64_t ax = v[1][0] - v[0][0], ay = v[1][1] - v[0][1], az = v[1][2] - v[0][2];
    int64_t bx = v[2][0] - v[1][0], by = v[2][1] - v[1][1], bz = v[2][2] - v[1][2];
    double nx = double(ay * bz - az * by), ny = double(az * bx - ax * bz), nz = double(ax * by - ay * bx);
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!len) return {0, 0, 0};
    return {float(nx / len), float(ny / len), float(nz / len)};
}

}
