#include "attack.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

static const uint32_t kPunching = 0x00100000, kKicking = 0x00200000, kTripping = 0x00400000;
static const float kReach = 90;
// the kick that ends the punches throws a person some 260 units and 50 up
static const float kFinisherPush = 60, kFinisherLift = 50;
// a slide kick takes people off their feet along the ground like bowling pins
static const float kSlidePush = 40, kSlideLift = 20;

Attack attack_now(uint32_t action, uint32_t flags) {
    switch (action) {
    // one action runs two punches and then a kick or a sweep
    case 0x00800380:
    case 0x00800457:
        return flags & (kKicking | kTripping) ? Attack::finisher : flags & kPunching ? Attack::punch : Attack::none;
    case 0x018008AC:
        return flags & kKicking ? Attack::kick : Attack::none;
    case 0x018008AA:
    case 0x0080045A:
        return Attack::slide;
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
    case Attack::finisher:
        return {1.2f, kReach * 1.15f, 0.5f, kFinisherPush, kFinisherLift, true};
    case Attack::slide:
        return {1.2f, kReach * 1.15f, 0.5f, kSlidePush, kSlideLift, true};
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

static const uint32_t kGroupMask = 0x1C0, kGroupAirborne = 0x080, kIntangible = 0x1000, kInvulnerable = 0x20000;

Stomp stomp_move(uint32_t action, float vel_y) {
    if ((action & kGroupMask) != kGroupAirborne || action & (kIntangible | kInvulnerable) || vel_y >= 0) return Stomp::none;
    return action == 0x008008A9 ? Stomp::pound : Stomp::stomp;
}

// game units, a stomp may land this far past the person's sides and this near the top
static const float kStompMargin = 10, kLanded = 1;

// how high the top of a box is over a spot, it slopes up from the edge at 45 degrees
static float top_at(const ActorBody &box, Vec3 p) {
    float fx = std::sin(box.heading), fy = std::cos(box.heading), dx = p.x - box.feet.x, dy = p.y - box.feet.y;
    float inside = std::min(box.half_length - std::fabs(dx * fx + dy * fy), box.half_width - std::fabs(dx * fy - dy * fx));
    float ridge = actor_ridge(box);
    return box.feet.z + box.height - ridge + std::clamp(inside, 0.0f, ridge);
}

bool stomps(float peak, Vec3 was, Vec3 now, const ActorBody &target, float scale) {
    ActorBody box = actor_box(target, scale), person = target;
    person.half_width += kStompMargin, person.half_length += kStompMargin;
    Vec3 n = nearest_on(person, now);
    if (peak < box.feet.z + box.height || now.z >= was.z || std::hypot(n.x - now.x, n.y - now.y) > 0.5f) return false;
    return was.z > top_at(box, was) + kLanded && now.z <= top_at(box, now) + kLanded;
}

// sm64 units a tick, what sm64 gives mario off a goomba, and its bounce sound
static const float kBounce = 30;
static const int32_t kSoundBounce = 0x0459B081;
static const uint32_t kActFreefall = 0x0100088C;

void stomp_bounce(int32_t id, const SM64MarioState &st) {
    sm64_set_mario_action(id, kActFreefall);
    sm64_set_mario_velocity(id, st.velocity[0], kBounce, st.velocity[2]);
    sm64_set_mario_forward_velocity(id, st.forwardVelocity);
    sm64_play_sound_global(kSoundBounce);
}

StompOutcome stomp_outcome(const StompTarget &t, Stomp move, const StompRules &rules) {
    // an essential foe would live on flattened, and a friend the story needs stays one
    if (t.essential) return t.hostile ? StompOutcome::hit : StompOutcome::stand;
    if (!t.hostile && rules.spare_friends) return StompOutcome::stand;
    if (t.height > rules.tallest) return StompOutcome::hit;
    if (move == Stomp::pound) return StompOutcome::gib;
    return t.skeleton ? StompOutcome::squash : StompOutcome::hit;
}

// sm64's goomba loses this much height a tick down to its flattest, then spreads out
static const float kSquashStep = 0.14f, kFlattest = 0.3f, kSpread = 1.7f;

Squash squash_at(int ticks) {
    float h = std::max(kFlattest, 1 - kSquashStep * std::max(ticks, 0));
    return {h, h <= kFlattest + 1e-4f ? kSpread : 1};
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

bool Thrown::resting(uint32_t target, int tick) const {
    auto it = last_.find(target);
    return it != last_.end() && tick - it->second < kTicks;
}

bool Thrown::allow(uint32_t target, int tick) {
    if (resting(target, tick)) return false;
    mark(target, tick);
    return true;
}

}
