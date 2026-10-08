#pragma once
#include "actors.h"

#include <cstdint>
#include <map>
#include <vector>

namespace sm64nv {

enum class Attack { none, punch, kick, dive, pound, finisher, slide };

// the blow mario is landing this tick, a punch or kick only counts while the limb is out
Attack attack_now(uint32_t action, uint32_t flags);

struct AttackProfile {
    // times the damage of one punch
    float damage;
    // game units from mario's middle to the nearest side of the target
    float range;
    // least cosine between his facing and the target, under -1 reaches all around
    float cone;
    // how hard the target is thrown back, 0 leaves it standing
    float push;
    // game units under mario's feet the throw comes from, which lifts the target off the ground
    float lift = 0;
    // throws every time, not only once the last throw's victim had time to get up
    bool always_throws = false;
};
AttackProfile attack_profile(Attack kind);
// what the game's melee skill curve makes of a blow, half at no skill and whole at 100
float unarmed_scale(float skill);

// whether a blow from mario at feet, that tall and facing heading, meets the body
bool attack_reaches(const AttackProfile &p, Vec3 feet, float height, float heading, const ActorBody &target);
// the spot on the body's sides nearest to p, across the ground
Vec3 nearest_on(const ActorBody &target, Vec3 p);

enum class Stomp { none, stomp, pound };
// whether mario is coming down of his own accord, a pound apart from any other fall
// judged before the tick, since the tick he lands on has him on his feet already
Stomp stomp_move(uint32_t action, float vel_y);
// his feet came down on the target's head this tick, from a fall that topped out at peak
bool stomps(float peak, Vec3 was, Vec3 now, const ActorBody &target, float scale);

// sends mario back up off a head with sm64's own bounce, he has landed on it already
void stomp_bounce(int32_t id, const SM64MarioState &st);

// what mario lands on, and what landing on it with a fall or a pound does
struct StompTarget {
    bool hostile, skeleton;
    float height;
    bool essential;
};
// what the player chose, by default people up to 1.3 times the usual height squash
struct StompRules {
    bool spare_friends = false;
    float tallest = 172;
};
enum class StompOutcome { stand, hit, squash, gib };
StompOutcome stomp_outcome(const StompTarget &t, Stomp move, const StompRules &rules);

// how flat and how wide a stomped person is so many ticks after
struct Squash {
    float height, width;
};
Squash squash_at(int ticks);

// one blow hits each target once however many ticks it lasts
class Swing {
public:
    void tick(Attack now);
    // true the first time this blow meets the target
    bool lands(uint32_t target);

private:
    Attack now_ = Attack::none;
    std::vector<uint32_t> hit_;
};

// a thrown target is left alone for a while so it can get up
class Thrown {
public:
    static constexpr int kTicks = 150;
    // true when the target may be thrown now, which also starts its rest
    bool allow(uint32_t target, int tick);
    // a throw that does not wait for the rest still starts one
    void mark(uint32_t target, int tick) { last_[target] = tick; }
    bool resting(uint32_t target, int tick) const;
    void clear() { last_.clear(); }

private:
    std::map<uint32_t, int> last_;
};

}
