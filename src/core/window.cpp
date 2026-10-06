#include "window.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

namespace {

// floors up to 78 over his feet still count as under him
const float kEyeHeight = 80;

// distance across the ground plane from p to the box around the surface
float flat_distance(const SM64Surface &s, Vec3 p) {
    float dx = 0, dz = 0;
    for (int axis : {0, 2}) {
        int lo = std::min({s.vertices[0][axis], s.vertices[1][axis], s.vertices[2][axis]});
        int hi = std::max({s.vertices[0][axis], s.vertices[1][axis], s.vertices[2][axis]});
        float v = axis ? p.z : p.x, d = v < lo ? lo - v : v > hi ? v - hi : 0;
        (axis ? dz : dx) = d;
    }
    return std::hypot(dx, dz);
}

// same cross product as the libsm64 surface loader
bool faces_away(const SM64Surface &s, Vec3 p) {
    const auto &v = s.vertices;
    int64_t ax = v[1][0] - v[0][0], ay = v[1][1] - v[0][1], az = v[1][2] - v[0][2];
    int64_t bx = v[2][0] - v[1][0], by = v[2][1] - v[1][1], bz = v[2][2] - v[1][2];
    double nx = double(ay * bz - az * by), ny = double(az * bx - ax * bz), nz = double(ax * by - ay * bx);
    return nx * (p.x - v[0][0]) + ny * (p.y - v[0][1]) + nz * (p.z - v[0][2]) < 0;
}

}

SurfaceWindow::SurfaceWindow(std::vector<SM64Surface> world, float radius, float reach)
    : world_(std::move(world)), radius_(radius), reach_(reach) {}

bool SurfaceWindow::update(Vec3 feet) {
    bool changed = !valid_ || std::hypot(feet.x - center_.x, feet.z - center_.z) > radius_ / 3;
    if (changed) {
        loaded_.clear(), source_.clear();
        for (uint32_t i = 0; i < world_.size(); i++)
            if (flat_distance(world_[i], feet) <= radius_) loaded_.push_back(world_[i]), source_.push_back(i);
        center_ = feet, valid_ = true;
        stats.gathers++;
    }
    Vec3 eye{feet.x, feet.y + kEyeHeight, feet.z};
    for (SM64Surface &s : loaded_) {
        if (flat_distance(s, feet) > reach_ || !faces_away(s, eye)) continue;
        std::swap(s.vertices[1], s.vertices[2]);
        stats.flips++, changed = true;
    }
    return changed;
}

}
