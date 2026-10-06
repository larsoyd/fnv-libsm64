#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

struct WindowStats {
    uint32_t gathers = 0, flips = 0;
};

// the surfaces around mario, the ones near him turned to face him
class SurfaceWindow {
public:
    // sm64 units measured across the ground plane, so floors far below still count
    // fixed marks surfaces of closed shapes that already face out and never turn
    SurfaceWindow(std::vector<SM64Surface> world, float radius, float reach, std::vector<bool> fixed = {});
    // true when the loaded set changed and libsm64 needs it again
    bool update(Vec3 feet);
    const std::vector<SM64Surface> &loaded() const { return loaded_; }
    uint32_t source(size_t i) const { return source_[i]; }
    // loaded surfaces whose bounding box comes within range of p
    std::vector<size_t> nearby(Vec3 p, float range) const;
    WindowStats stats;

private:
    std::vector<SM64Surface> world_, loaded_;
    std::vector<uint32_t> source_;
    std::vector<bool> fixed_;
    float radius_, reach_;
    Vec3 center_{};
    bool valid_ = false;
};

}
