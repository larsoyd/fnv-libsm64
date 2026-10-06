#include "window.h"
#include "surfaces.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

namespace {

// floors up to 78 over his feet still count as under him
const float kStepHeight = 78, kEyeHeight = 80;

// distance to the box around the surface, height only counts when asked
float box_distance(const SM64Surface &s, Vec3 p, bool height) {
    const float at[3] = {p.x, p.y, p.z};
    float sum = 0;
    for (int axis = 0; axis < 3; axis++) {
        if (axis == 1 && !height) continue;
        int lo = std::min({s.vertices[0][axis], s.vertices[1][axis], s.vertices[2][axis]});
        int hi = std::max({s.vertices[0][axis], s.vertices[1][axis], s.vertices[2][axis]});
        float d = at[axis] < lo ? lo - at[axis] : at[axis] > hi ? at[axis] - hi : 0;
        sum += d * d;
    }
    return std::sqrt(sum);
}

bool faces_away(const SM64Surface &s, Vec3 p) {
    Vec3 n = surface_normal(s);
    const auto &v = s.vertices[0];
    return n.x * (p.x - v[0]) + n.y * (p.y - v[1]) + n.z * (p.z - v[2]) < 0;
}

}

SurfaceWindow::SurfaceWindow(std::vector<SM64Surface> world, float radius, float reach, std::vector<bool> fixed)
    : world_(std::move(world)), fixed_(std::move(fixed)), settled_(world_.size()), sides_(world_), radius_(radius), reach_(reach) {
    fixed_.resize(world_.size());
}

bool SurfaceWindow::settle(size_t i) {
    uint32_t w = source_[i];
    if (settled_[w]) return false;
    settled_[w] = true;
    int side = fixed_[w] ? 0 : sides_.open_side(world_[w]);
    if (!side) return false;
    if (side < 0) std::swap(world_[w].vertices[1], world_[w].vertices[2]), loaded_[i] = world_[w];
    fixed_[w] = true, stats.settled++;
    return true;
}

std::vector<size_t> SurfaceWindow::nearby(Vec3 p, float range) const {
    std::vector<size_t> out;
    for (size_t i = 0; i < loaded_.size(); i++)
        if (box_distance(loaded_[i], p, true) <= range) out.push_back(i);
    return out;
}

float SurfaceWindow::headroom(Vec3 feet) const {
    float under = -INFINITY, over = INFINITY, h;
    for (const SM64Surface &s : loaded_) {
        if (!height_at(s, feet.x, feet.z, h)) continue;
        if (h <= feet.y + kStepHeight) under = std::fmax(under, h);
        else over = std::fmin(over, h);
    }
    return over - under;
}

bool SurfaceWindow::update(Vec3 feet) {
    bool changed = !valid_ || std::hypot(feet.x - center_.x, feet.z - center_.z) > radius_ / 3;
    if (changed) {
        loaded_.clear(), source_.clear();
        for (uint32_t i = 0; i < world_.size(); i++)
            if (box_distance(world_[i], feet, false) <= radius_) loaded_.push_back(world_[i]), source_.push_back(i);
        center_ = feet, valid_ = true;
        stats.gathers++;
    }
    Vec3 eye{feet.x, feet.y + kEyeHeight, feet.z};
    for (size_t i = 0; i < loaded_.size(); i++) {
        SM64Surface &s = loaded_[i];
        if (box_distance(s, feet, false) > reach_) continue;
        changed |= settle(i);
        if (fixed(i) || std::fabs(surface_normal(s).y) <= 0.01f || !faces_away(s, eye)) continue;
        std::swap(s.vertices[1], s.vertices[2]);
        stats.flips++, changed = true;
    }
    return changed;
}

}
