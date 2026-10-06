#include "hull.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

static Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 scaled(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

struct Plane {
    Vec3 n;
    float d;
};

static float extent_of(const std::vector<Vec3> &pts) {
    Vec3 lo = pts[0], hi = pts[0];
    for (Vec3 p : pts) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    return std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
}

static void fan_face(const std::vector<Vec3> &pts, Plane pl, float eps, std::vector<Tri> &out) {
    std::vector<Vec3> face;
    for (Vec3 p : pts) {
        if (std::fabs(dot(pl.n, p) - pl.d) > eps) continue;
        bool dup = false;
        for (Vec3 q : face) dup = dup || dot(sub(p, q), sub(p, q)) <= eps * eps;
        if (!dup) face.push_back(p);
    }
    if (face.size() < 3) return;
    Vec3 c{0, 0, 0};
    for (Vec3 p : face) c = {c.x + p.x, c.y + p.y, c.z + p.z};
    c = scaled(c, 1.0f / face.size());
    Vec3 u = sub(face[0], c);
    u = scaled(u, 1.0f / std::sqrt(dot(u, u)));
    Vec3 v = cross(pl.n, u);
    std::sort(face.begin(), face.end(), [&](Vec3 a, Vec3 b) {
        return std::atan2(dot(sub(a, c), v), dot(sub(a, c), u)) < std::atan2(dot(sub(b, c), v), dot(sub(b, c), u));
    });
    for (size_t m = 1; m + 1 < face.size(); m++) out.push_back({face[0], face[m], face[m + 1], 0, true});
}

std::vector<Tri> convex_hull(const std::vector<Vec3> &pts) {
    std::vector<Tri> out;
    if (pts.size() < 4) return out;
    float eps = extent_of(pts) * 1e-4f + 1e-7f;
    std::vector<Plane> planes;
    size_t n = pts.size();
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            for (size_t k = j + 1; k < n; k++) {
                Vec3 nrm = cross(sub(pts[j], pts[i]), sub(pts[k], pts[i]));
                float len = std::sqrt(dot(nrm, nrm));
                if (len <= eps * eps) continue;
                nrm = scaled(nrm, 1 / len);
                float d = dot(nrm, pts[i]);
                bool above = false, below = false;
                for (size_t m = 0; m < n && !(above && below); m++) {
                    float s = dot(nrm, pts[m]) - d;
                    above = above || s > eps;
                    below = below || s < -eps;
                }
                if (above == below) continue;
                if (above) nrm = scaled(nrm, -1), d = -d;
                bool seen = false;
                for (const Plane &p : planes) seen = seen || (dot(p.n, nrm) > 1 - 1e-5f && std::fabs(p.d - d) <= eps);
                if (!seen) planes.push_back({nrm, d});
            }
    for (const Plane &p : planes) fan_face(pts, p, eps, out);
    return out;
}

std::vector<Tri> box_tris(Vec3 c, Vec3 h) {
    std::vector<Vec3> corners;
    for (int i = 0; i < 8; i++)
        corners.push_back({c.x + (i & 1 ? h.x : -h.x), c.y + (i & 2 ? h.y : -h.y), c.z + (i & 4 ? h.z : -h.z)});
    return convex_hull(corners);
}

}
