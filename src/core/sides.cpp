#include "sides.h"
#include "surfaces.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sm64nv {

namespace {

// sm64 units, mario's height and the reach of his lower wall check
const float kTall = 160, kRadius = 24;
// a wall is looked at from where mario's middle would be when he leans on it
const float kSkin = kRadius + 1;
const float kCell = 64;

}

Sides::Sides(std::vector<SM64Surface> all) : all_(std::move(all)) {
    if (all_.empty()) return;
    float lo[2] = {INFINITY, INFINITY}, hi[2] = {-INFINITY, -INFINITY};
    for (const SM64Surface &s : all_) {
        Box b;
        for (int k = 0; k < 3; k++) {
            b.lo[k] = (float)std::min({s.vertices[0][k], s.vertices[1][k], s.vertices[2][k]});
            b.hi[k] = (float)std::max({s.vertices[0][k], s.vertices[1][k], s.vertices[2][k]});
        }
        boxes_.push_back(b);
        for (int k = 0; k < 2; k++) lo[k] = std::fmin(lo[k], b.lo[2 * k]), hi[k] = std::fmax(hi[k], b.hi[2 * k]);
    }
    // each square lists what lies within his radius of it, so a probe only reads its own square
    x0_ = lo[0] - kRadius, z0_ = lo[1] - kRadius;
    nx_ = int((hi[0] + kRadius - x0_) / kCell) + 1, nz_ = int((hi[1] + kRadius - z0_) / kCell) + 1;
    cells_.resize(size_t(nx_) * nz_);
    for (uint32_t i = 0; i < all_.size(); i++)
        for (int z = row(boxes_[i].lo[2] - kRadius); z <= row(boxes_[i].hi[2] + kRadius); z++)
            for (int x = column(boxes_[i].lo[0] - kRadius); x <= column(boxes_[i].hi[0] + kRadius); x++)
                cells_[size_t(z) * nx_ + x].push_back(i);
}

int Sides::column(float x) const { return std::clamp(int((x - x0_) / kCell), 0, nx_ - 1); }
int Sides::row(float z) const { return std::clamp(int((z - z0_) / kCell), 0, nz_ - 1); }
const std::vector<uint32_t> &Sides::at(Point p) const { return cells_[size_t(row(p.z)) * nx_ + column(p.x)]; }

// free height at p over the nearest surface under it, none when nothing is under it
float Sides::room(Point p) const {
    float under = -INFINITY, over = INFINITY, h;
    for (uint32_t i : at(p)) {
        const Box &b = boxes_[i];
        if (p.x < b.lo[0] || p.x > b.hi[0] || p.z < b.lo[2] || p.z > b.hi[2] || !height_at(all_[i], p.x, p.z, h)) continue;
        if (h <= p.y) under = std::fmax(under, h);
        else over = std::fmin(over, h);
    }
    return std::isinf(under) ? 0 : over - under;
}

// whether anything crosses the line from p along the unit direction d within his radius
bool Sides::blocked(Point p, Point d) const {
    const float from[3] = {p.x, p.y, p.z}, to[3] = {p.x + d.x * kRadius, p.y + d.y * kRadius, p.z + d.z * kRadius};
    for (uint32_t i : at(p)) {
        const Box &b = boxes_[i];
        bool apart = false;
        for (int k = 0; k < 3; k++) apart |= std::fmax(from[k], to[k]) < b.lo[k] || std::fmin(from[k], to[k]) > b.hi[k];
        if (apart) continue;
        const auto &v = all_[i].vertices;
        float ux = v[1][0] - v[0][0], uy = v[1][1] - v[0][1], uz = v[1][2] - v[0][2];
        float vx = v[2][0] - v[0][0], vy = v[2][1] - v[0][1], vz = v[2][2] - v[0][2];
        float hx = d.y * vz - d.z * vy, hy = d.z * vx - d.x * vz, hz = d.x * vy - d.y * vx;
        float det = ux * hx + uy * hy + uz * hz;
        if (std::fabs(det) < 1e-6f) continue;
        float wx = p.x - v[0][0], wy = p.y - v[0][1], wz = p.z - v[0][2];
        float qx = wy * uz - wz * uy, qy = wz * ux - wx * uz, qz = wx * uy - wy * ux;
        float s = (wx * hx + wy * hy + wz * hz) / det, t = (d.x * qx + d.y * qy + d.z * qz) / det;
        float hit = (vx * qx + vy * qy + vz * qz) / det;
        if (s >= 0 && t >= 0 && s + t <= 1 && hit > 0 && hit <= kRadius) return true;
    }
    return false;
}

// tall enough for mario at p, with nothing inside his radius ahead along n or to either hand
bool Sides::fits(Point p, Point n) const {
    return room(p) >= kTall && !blocked(p, n) && !blocked(p, {-n.z, 0, n.x}) && !blocked(p, {n.z, 0, -n.x});
}

// room for mario somewhere on the side of the wall that n points to
bool Sides::open(const SM64Surface &wall, Point n) const {
    const auto &v = wall.vertices;
    Point mid{(v[0][0] + v[1][0] + v[2][0]) / 3.0f, (v[0][1] + v[1][1] + v[2][1]) / 3.0f, (v[0][2] + v[1][2] + v[2][2]) / 3.0f};
    if (fits({mid.x + n.x * kSkin, mid.y, mid.z + n.z * kSkin}, n)) return true;
    // then most of the way out to each corner, one of them may clear what covers the middle
    for (const auto &c : v) {
        Point on{mid.x + (c[0] - mid.x) * 0.8f, mid.y + (c[1] - mid.y) * 0.8f, mid.z + (c[2] - mid.z) * 0.8f};
        if (fits({on.x + n.x * kSkin, on.y, on.z + n.z * kSkin}, n)) return true;
    }
    return false;
}

int Sides::open_side(const SM64Surface &wall) const {
    Vec3 n = surface_normal(wall);
    if ((!n.x && !n.z) || std::fabs(n.y) > 0.01f) return 0;
    bool front = open(wall, {n.x, 0, n.z}), back = open(wall, {-n.x, 0, -n.z});
    return front == back ? 0 : front ? 1 : -1;
}

}
