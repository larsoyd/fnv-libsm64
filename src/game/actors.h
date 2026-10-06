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

// characters and creatures on their feet within reach of center, nearest first
std::vector<LiveActor> nearby_actors(fnv::TESObjectCELL *cell, Vec3 center, float reach, ActorStats &stats);

// how far the game has an actor knocked off its feet, 0 when it stands
int knocked(fnv::TESObjectREFR *actor);
// health as the game counts it
float actor_health(fnv::TESObjectREFR *actor);
// a bare handed blow by the player through the game's own hit handling
// damage is what it does before armour, what the game made of it comes back
float strike(fnv::TESObjectREFR *target, float damage);
// throws the actor away from a spot like the game's own shove, false for one that cannot be
bool shove(fnv::TESObjectREFR *target, Vec3 from, float force);

}
