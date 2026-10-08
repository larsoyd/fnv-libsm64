#include "reach.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

Vec3 nearest_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
    auto sub = [](Vec3 u, Vec3 v) { return Vec3{u.x - v.x, u.y - v.y, u.z - v.z}; };
    auto dot = [](Vec3 u, Vec3 v) { return u.x * v.x + u.y * v.y + u.z * v.z; };
    Vec3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    float bb = dot(ab, ab), cc = dot(ac, ac), bc = dot(ab, ac), pb = dot(ap, ab), pc = dot(ap, ac);
    float det = bb * cc - bc * bc;
    if (det > 0) {
        float u = (cc * pb - bc * pc) / det, v = (bb * pc - bc * pb) / det;
        if (u >= 0 && v >= 0 && u + v <= 1)
            return {a.x + ab.x * u + ac.x * v, a.y + ab.y * u + ac.y * v, a.z + ab.z * u + ac.z * v};
    }
    Vec3 best = a;
    float distance = dot(ap, ap);
    for (auto [start, end] : {std::pair{a, b}, std::pair{b, c}, std::pair{c, a}}) {
        Vec3 edge = sub(end, start), delta = sub(p, start);
        float length = dot(edge, edge);
        float t = length > 0 ? std::clamp(dot(delta, edge) / length, 0.0f, 1.0f) : 0;
        Vec3 q{start.x + edge.x * t, start.y + edge.y * t, start.z + edge.z * t};
        Vec3 away = sub(p, q);
        float d = dot(away, away);
        if (d < distance) best = q, distance = d;
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
