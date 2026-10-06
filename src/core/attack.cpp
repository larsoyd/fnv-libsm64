#include "attack.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

static const uint32_t kPunching = 0x00100000, kKicking = 0x00200000, kTripping = 0x00400000;
static const float kReach = 90;

Attack attack_now(uint32_t action, uint32_t flags) {
    switch (action) {
    // one action runs two punches and then a kick or a sweep
    case 0x00800380:
    case 0x00800457:
    case 0x018008AC:
        return flags & (kKicking | kTripping) ? Attack::kick : flags & kPunching ? Attack::punch : Attack::none;
    case 0x018008AA:
    case 0x0080045A:
        return Attack::kick;
    // sm64 does not count the long jump as an attack, here it lands like a dive
    case 0x0188088A:
    case 0x00880456:
    case 0x03000888:
        return Attack::dive;
    case 0x008008A9:
    case 0x0080023C:
        return Attack::pound;
    default:
        return Attack::none;
    }
}

AttackProfile attack_profile(Attack kind) {
    switch (kind) {
    case Attack::punch:
        return {1, kReach, 0.5f, 0};
    case Attack::kick:
        return {1.2f, kReach * 1.15f, 0.5f, 6};
    case Attack::dive:
        return {1.1f, kReach * 1.35f, 0.25f, 5};
    case Attack::pound:
        return {1.5f, kReach * 0.8f, -2, 8};
    default:
        return {};
    }
}

float unarmed_scale(float skill) { return 0.5f + 0.5f * std::clamp(skill, 0.0f, 100.0f) / 100; }

Vec3 nearest_on(const ActorBody &t, Vec3 p) {
    float fx = std::sin(t.heading), fy = std::cos(t.heading), dx = p.x - t.feet.x, dy = p.y - t.feet.y;
    float along = std::clamp(dx * fx + dy * fy, -t.half_length, t.half_length);
    float across = std::clamp(dx * fy - dy * fx, -t.half_width, t.half_width);
    return {t.feet.x + fx * along + fy * across, t.feet.y + fy * along - fx * across, p.z};
}

bool attack_reaches(const AttackProfile &p, Vec3 feet, float height, float heading, const ActorBody &t) {
    if (t.feet.z + t.height < feet.z || t.feet.z > feet.z + height) return false;
    Vec3 n = nearest_on(t, feet);
    float dx = n.x - feet.x, dy = n.y - feet.y, gap = std::hypot(dx, dy);
    if (gap > p.range) return false;
    // standing in or on the target there is no way to it that he could face
    if (gap <= 1) return true;
    return (dx * std::sin(heading) + dy * std::cos(heading)) / gap >= p.cone;
}

void Swing::tick(Attack now) {
    if (now != now_) hit_.clear();
    now_ = now;
}

bool Swing::lands(uint32_t target) {
    if (now_ == Attack::none || std::ranges::count(hit_, target)) return false;
    hit_.push_back(target);
    return true;
}

bool Thrown::allow(uint32_t target, int tick) {
    auto it = last_.find(target);
    if (it != last_.end() && tick - it->second < kTicks) return false;
    last_[target] = tick;
    return true;
}

}
