#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

// something alive near mario, in game units and radians
struct ActorBody {
    uint32_t id;
    Vec3 feet;
    float heading;
    float half_width, half_length, height;
};

// the box libsm64 holds for a body in game units, never narrower than mario's wall probe
ActorBody actor_box(const ActorBody &a, float scale);
// how far under the top of its box a body's ridge starts to slope
float actor_ridge(const ActorBody &box);

// upright sides under a ridge too steep to stand on, in sm64 units around the feet
std::vector<SM64Surface> actor_surfaces(float half_x, float half_z, float height);

// one moving shape in libsm64 for each body, made, moved and dropped as they come and go
class ActorBoxes {
public:
    static constexpr size_t kMax = 32;

    ~ActorBoxes() { clear(); }
    void sync(const Frame &f, const std::vector<ActorBody> &bodies);
    void clear() { sync({}, {}); }
    size_t size() const { return boxes_.size(); }

private:
    struct Box {
        uint32_t id, object;
        float half_width, half_length, height;
    };
    std::vector<Box> boxes_;
};

}
