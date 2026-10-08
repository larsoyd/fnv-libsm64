#pragma once
#include "core/actors.h"
#include "game/fnv.h"

#include <vector>

namespace sm64nv {

struct LiveActor {
    fnv::TESObjectREFR *ref;
    ActorBody body;
    // where its size came from: its record, the size people are, or its body in physics
    const char *sized;
};

struct ActorStats {
    // actors seen in the loaded cells, then why some are left out
    int seen = 0, away = 0, down = 0, unsized = 0;
};

// the actor of a base form made last, standing or not, null when there is none
fnv::TESObjectREFR *newest_of_base(fnv::TESObjectCELL *cell, uint32_t base);

// characters and creatures on their feet within reach of center, nearest first
std::vector<LiveActor> nearby_actors(fnv::TESObjectCELL *cell, Vec3 center, float reach, ActorStats &stats);

// how far the game has an actor knocked off its feet, 0 when it stands
int knocked(fnv::TESObjectREFR *actor);
// alive and on its feet, restrained counts, dying and dead do not
bool actor_standing(const fnv::TESObjectREFR *actor);
// a character rather than a creature
bool is_person(const fnv::TESObjectREFR *actor);

// a skeleton's root, which animation leaves alone, so scaling it scales the body
struct Skeleton {
    void *root = nullptr;
    float rot[9];
};
Skeleton skeleton_of(fnv::TESObjectREFR *actor);
// height across the skeleton's own up and width along the ground, 1 puts it back as it was
void squash_skeleton(const Skeleton &s, float height, float width);
// how high the head is over the feet as the body is drawn, -1 without a head
float head_height(fnv::TESObjectREFR *actor);
// where the body's middle is drawn, a fallen body leaves its reference behind
bool pelvis_at(fnv::TESObjectREFR *actor, Vec3 &out);

// health as the game counts it, and the most it can hold
float actor_health(fnv::TESObjectREFR *actor);
float actor_max_health(fnv::TESObjectREFR *actor);
// the unarmed skill as the game counts it now
float actor_unarmed(fnv::TESObjectREFR *actor);
// a bare handed blow by the player through the game's own hit handling
// damage is what it does before armour, what the game made of it comes back
float strike(fnv::TESObjectREFR *target, float damage);
// throws the actor away from a spot like the game's own shove, false for one that cannot be
bool shove(fnv::TESObjectREFR *target, Vec3 from, float force);

}
