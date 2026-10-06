#include "regather.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

float Regather::moved(Vec3 at) const { return std::hypot(at.x - center_.x, at.y - center_.y, at.z - center_.z); }

bool Regather::due(Vec3 at, int tick) const { return tick >= wait_until_ && moved(at) > move_; }

void Regather::loaded(Vec3 center) { center_ = center, wait_until_ = 0; }

void Regather::refused(int tick) { wait_until_ = tick + kRetryTicks; }

uint64_t pose_key(const float rotation[9], Vec3 at) {
    uint64_t h = 1469598103934665603ull;
    auto fold = [&](long v) { h = (h ^ uint64_t(v)) * 1099511628211ull; };
    for (int i = 0; i < 9; i++) fold(std::lround(rotation[i] * 100));
    fold(std::lround(at.x * 4)), fold(std::lround(at.y * 4)), fold(std::lround(at.z * 4));
    return h;
}

void DoorPoses::loaded(std::vector<Pose> poses) { std::sort(poses.begin(), poses.end()), poses_ = std::move(poses); }

bool DoorPoses::changed(std::vector<Pose> now) const { return std::sort(now.begin(), now.end()), now != poses_; }

}
