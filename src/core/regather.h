#pragma once
#include "frame.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace sm64nv {

// when the world is gathered again around mario
class Regather {
public:
    static constexpr int kRetryTicks = 15;

    explicit Regather(float move) : move_(move) {}
    // how far he is from the middle of what is loaded
    float moved(Vec3 at) const;
    bool due(Vec3 at, int tick) const;
    void loaded(Vec3 center);
    // what was loaded before stays, so its middle does too
    void refused(int tick);

private:
    float move_;
    Vec3 center_{};
    int wait_until_ = 0;
};

// a pose rounded so that a door read twice while still gives the same key
uint64_t pose_key(const float rotation[9], Vec3 at);

// the doors of the place by form id with the pose they had when the world was gathered
class DoorPoses {
public:
    using Pose = std::pair<uint32_t, uint64_t>;
    void loaded(std::vector<Pose> poses);
    // one moved, came or went since then
    bool changed(std::vector<Pose> now) const;

private:
    std::vector<Pose> poses_;
};

}
