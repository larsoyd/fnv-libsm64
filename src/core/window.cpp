#include "window.h"
#include "surfaces.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

namespace {

// floors up to 78 over his feet still count as under him
const float kEyeHeight = 80;
// walls wholly under his feet or over his head keep their side
const float kHeight = 160;

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

bool spans(const SM64Surface &s, float lo, float hi) {
    int a = std::min({s.vertices[0][1], s.vertices[1][1], s.vertices[2][1]});
    int b = std::max({s.vertices[0][1], s.vertices[1][1], s.vertices[2][1]});
    return b > lo && a < hi;
}

bool faces_away(const SM64Surface &s, Vec3 p) {
    Vec3 n = surface_normal(s);
    const auto &v = s.vertices[0];
    return n.x * (p.x - v[0]) + n.y * (p.y - v[1]) + n.z * (p.z - v[2]) < 0;
}

}

SurfaceWindow::SurfaceWindow(std::vector<SM64Surface> world, float radius, float reach, std::vector<bool> fixed)
    : world_(std::move(world)), fixed_(std::move(fixed)), radius_(radius), reach_(reach) {}

std::vector<size_t> SurfaceWindow::nearby(Vec3 p, float range) const {
    std::vector<size_t> out;
    for (size_t i = 0; i < loaded_.size(); i++)
        if (box_distance(loaded_[i], p, true) <= range) out.push_back(i);
    return out;
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
        if (!fixed_.empty() && fixed_[source_[i]]) continue;
        if (box_distance(s, feet, false) > reach_ || !faces_away(s, eye)) continue;
        if (std::fabs(surface_normal(s).y) <= 0.01f && !spans(s, feet.y + 1, feet.y + kHeight)) continue;
        std::swap(s.vertices[1], s.vertices[2]);
        stats.flips++, changed = true;
    }
    return changed;
}

}
