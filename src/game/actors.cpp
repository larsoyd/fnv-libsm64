#include "game/actors.h"
#include "game/collision.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

namespace {

const uintptr_t kVtblCharacter = 0x01086A6C, kVtblCreature = 0x010870AC;
// alive and restrained both stand, anything else is on the ground
const uint32_t kAlive = 0, kRestrained = 5;
// game units, a bound outside this is not a body
const float kLeastSize = 4, kMostSize = 2000;

template <typename T> T at(const void *base, size_t off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off);
}

bool is_actor(const fnv::TESObjectREFR *ref) {
    uintptr_t vt = fnv::vtbl_of(ref);
    return (vt == kVtblCharacter || vt == kVtblCreature) && ref->baseForm && ref->renderState && ref->renderState->niNode;
}

bool standing(const fnv::TESObjectREFR *ref) {
    uint32_t life = at<uint32_t>(ref, 0x108);
    return life == kAlive || life == kRestrained;
}

// the base form's bounds are six shorts, low corner then high corner
bool sized(const fnv::TESObjectREFR *ref, ActorBody &b) {
    auto *lo = reinterpret_cast<const int16_t *>(reinterpret_cast<const uint8_t *>(ref->baseForm) + 0x24), *hi = lo + 3;
    float size[3] = {(hi[0] - lo[0]) * ref->scale, (hi[1] - lo[1]) * ref->scale, hi[2] * ref->scale};
    for (float s : size)
        if (!(s >= kLeastSize && s <= kMostSize)) return false;
    b.half_width = size[0] / 2, b.half_length = size[1] / 2, b.height = size[2];
    return true;
}

float apart(const LiveActor &a, Vec3 c) { return std::hypot(a.body.feet.x - c.x, a.body.feet.y - c.y, a.body.feet.z - c.z); }

}

std::vector<LiveActor> nearby_actors(fnv::TESObjectCELL *cell, Vec3 c, float reach, ActorStats &stats) {
    std::vector<LiveActor> out;
    for (fnv::TESObjectCELL *one : loaded_cells(cell))
        for (auto *it = &one->objectList; it; it = it->next) {
            fnv::TESObjectREFR *ref = it->data;
            if (!is_actor(ref)) continue;
            stats.seen++;
            LiveActor a{ref, {ref->form.refID, {ref->pos[0], ref->pos[1], ref->pos[2]}, ref->rot[2], 0, 0, 0}};
            if (apart(a, c) > reach) stats.away++;
            else if (!standing(ref)) stats.down++;
            else if (!sized(ref, a.body)) stats.unsized++;
            else out.push_back(a);
        }
    std::ranges::sort(out, {}, [&](const LiveActor &a) { return apart(a, c); });
    return out;
}

}
