#include "surfaces.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <map>

namespace sm64nv {

static const uint16_t kTerrainStone = 1;
// past about 78 degrees a face is meant as a wall but sm64 wants it within 0.01
static const float kNearlyUpright = 0.2f;
static const int16_t kNotSlippery = 0x15;
// sm64 slides him off floors past 38 degrees, a little more here to allow for rounding
static const float kSlideUp = 0.8f;
// what he steps up without a jump, and how far along a slope its rise is measured
static const float kStepRise = 78, kStepNear = 150;

namespace {

struct Steep {
    uint32_t surface;
    float x, z;
    int32_t lo, hi;
};

// a kerb or a tilted slab is steep but short, walking over it should not start a slide
void mark_steps(std::vector<SM64Surface> &all, SurfaceStats &stats) {
    std::vector<Steep> steep;
    std::map<std::array<int32_t, 3>, std::vector<uint32_t>> at;
    for (uint32_t i = 0; i < all.size(); i++) {
        float up = std::fabs(surface_normal(all[i]).y);
        if (up <= 0.01f || up > kSlideUp) continue;
        const auto &v = all[i].vertices;
        for (const auto &c : v) at[{c[0], c[1], c[2]}].push_back((uint32_t)steep.size());
        steep.push_back({i, (v[0][0] + v[1][0] + v[2][0]) / 3.0f, (v[0][2] + v[1][2] + v[2][2]) / 3.0f,
                         std::min({v[0][1], v[1][1], v[2][1]}), std::max({v[0][1], v[1][1], v[2][1]})});
    }
    std::vector<uint32_t> seen(steep.size(), UINT32_MAX), open;
    for (uint32_t i = 0; i < steep.size(); i++) {
        // the steep faces joined to this one corner to corner, as far as they stay near it
        int32_t lo = steep[i].lo, hi = steep[i].hi;
        open.assign(1, i), seen[i] = i;
        while (!open.empty() && hi - lo <= kStepRise) {
            uint32_t k = open.back();
            open.pop_back();
            lo = std::min(lo, steep[k].lo), hi = std::max(hi, steep[k].hi);
            for (const auto &c : all[steep[k].surface].vertices)
                for (uint32_t n : at[{c[0], c[1], c[2]}]) {
                    if (seen[n] == i || std::hypot(steep[n].x - steep[i].x, steep[n].z - steep[i].z) > kStepNear) continue;
                    seen[n] = i, open.push_back(n);
                }
        }
        if (hi - lo > kStepRise) continue;
        all[steep[i].surface].type = kNotSlippery, stats.steps++;
    }
}

}

bool reaches(const Tri &t, Vec3 lo, Vec3 hi) {
    // by its bounds, a floor can span the whole box with no corner in it
    auto apart = [&](float Vec3::*k) { return std::max({t.a.*k, t.b.*k, t.c.*k}) < lo.*k || std::min({t.a.*k, t.b.*k, t.c.*k}) > hi.*k; };
    return !apart(&Vec3::x) && !apart(&Vec3::y) && !apart(&Vec3::z);
}

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
    mark_steps(out, stats);
    return out;
}

bool stand_up(SM64Surface &s) {
    Vec3 n = surface_normal(s);
    float len = std::hypot(n.x, n.z), hx = n.x / len, hz = n.z / len, fx[3], fz[3];
    // a ledge over the face ends at its top so the wall goes there
    const auto &top = *std::max_element(std::begin(s.vertices), std::end(s.vertices), [](auto &a, auto &b) { return a[1] < b[1]; });
    for (int i = 0; i < 3; i++) {
        // half a unit off the corner, so it can round either way like the other two
        float d = (s.vertices[i][0] - top[0]) * hx + (s.vertices[i][2] - top[2]) * hz - 0.5f;
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

bool height_at(const SM64Surface &s, float x, float z, float &height) {
    const auto &v = s.vertices;
    float ux = v[1][0] - v[0][0], uz = v[1][2] - v[0][2], wx = v[2][0] - v[0][0], wz = v[2][2] - v[0][2];
    float area = ux * wz - uz * wx, px = x - v[0][0], pz = z - v[0][2];
    if (!area) return false;
    float a = (px * wz - pz * wx) / area, b = (ux * pz - uz * px) / area;
    if (a < 0 || b < 0 || a + b > 1) return false;
    height = v[0][1] + a * (v[1][1] - v[0][1]) + b * (v[2][1] - v[0][1]);
    return true;
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
