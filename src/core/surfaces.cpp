#include "surfaces.h"

#include <cmath>

namespace sm64nv {

static const uint16_t kTerrainStone = 1;
// past about 78 degrees a face is meant as a wall but sm64 wants it within 0.01
static const float kNearlyUpright = 0.2f;

std::vector<Tri> quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { return {{a, b, c}, {a, c, d}}; }

std::vector<SM64Surface> build_surfaces(const Frame &f, const std::vector<Tri> &tris, SurfaceStats &stats,
                                        std::vector<uint32_t> *kept) {
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
        if (std::fabs(n.y) > 0.01f && std::fabs(n.y) < kNearlyUpright) {
            stats.stood_up++;
            stats.still_steep += !stand_up(s);
            n = surface_normal(s);
        }
        if (n.y > 0.01f) stats.floors++;
        else if (n.y < -0.01f) stats.ceilings++;
        else stats.walls++;
        out.push_back(s);
        if (kept) kept->push_back(uint32_t(&t - tris.data()));
    }
    return out;
}

bool stand_up(SM64Surface &s) {
    Vec3 n = surface_normal(s);
    float len = std::hypot(n.x, n.z), hx = n.x / len, hz = n.z / len, cx = 0, cz = 0, fx[3], fz[3];
    for (const auto &v : s.vertices) cx += v[0] / 3.0f, cz += v[2] / 3.0f;
    for (int i = 0; i < 3; i++) {
        float d = (s.vertices[i][0] - cx) * hx + (s.vertices[i][2] - cz) * hz;
        fx[i] = s.vertices[i][0] - d * hx, fz[i] = s.vertices[i][2] - d * hz;
    }
    // whole unit corners can tip it past 0.01 again so try every way of rounding them
    SM64Surface best = s;
    float best_up = 2;
    for (int m = 0; m < 64; m++) {
        SM64Surface t = s;
        for (int i = 0; i < 3; i++) {
            t.vertices[i][0] = int32_t(m >> 2 * i & 1 ? std::ceil(fx[i]) : std::floor(fx[i]));
            t.vertices[i][2] = int32_t(m >> (2 * i + 1) & 1 ? std::ceil(fz[i]) : std::floor(fz[i]));
        }
        Vec3 tn = surface_normal(t);
        if ((tn.x || tn.z) && std::fabs(tn.y) < best_up) best_up = std::fabs(tn.y), best = t;
    }
    s = best;
    return best_up <= 0.01f;
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
