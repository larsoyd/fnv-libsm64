#pragma once
#include "core/frame.h"
#include "game/fnv.h"

#include <vector>

namespace sm64nv {

struct Door {
    fnv::TESObjectREFR *ref;
    Vec3 pos;
    // radians, the way the door faces when shut
    float heading;
    bool teleports;
};

// door references of the cell that are drawn
std::vector<Door> cell_doors(fnv::TESObjectCELL *cell);
// the player uses the reference the way the activate key does
bool activate(fnv::TESObjectREFR *ref);

}
