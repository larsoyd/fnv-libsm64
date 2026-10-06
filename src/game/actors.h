#pragma once
#include "core/actors.h"
#include "game/fnv.h"

#include <vector>

namespace sm64nv {

struct LiveActor {
    fnv::TESObjectREFR *ref;
    ActorBody body;
};

struct ActorStats {
    // actors seen in the loaded cells, then why some are left out
    int seen = 0, away = 0, down = 0, unsized = 0;
};

// characters and creatures on their feet within reach of center, nearest first
std::vector<LiveActor> nearby_actors(fnv::TESObjectCELL *cell, Vec3 center, float reach, ActorStats &stats);

}
