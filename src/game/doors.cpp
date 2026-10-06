#include "game/doors.h"
#include "game/rtti.h"

namespace sm64nv {

namespace {

const uint8_t kFormDoor = 0x1C;
const int kMaxExtras = 64;

using Activate = bool(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t);

bool teleports(const fnv::TESObjectREFR *ref) {
    // the list keeps its first entry one word in and each entry its next two words in
    const void *x = *reinterpret_cast<void *const *>(ref->extraDataList + 4);
    for (int i = 0; i < kMaxExtras && rtti_is(x, ".?AVBSExtraData@@"); i++) {
        if (rtti_is(x, ".?AVExtraTeleport@@")) return true;
        x = *reinterpret_cast<void *const *>(static_cast<const uint8_t *>(x) + 8);
    }
    return false;
}

}

std::vector<Door> cell_doors(fnv::TESObjectCELL *cell) {
    std::vector<Door> out;
    for (auto *it = &cell->objectList; it; it = it->next) {
        fnv::TESObjectREFR *ref = it->data;
        if (!ref || !ref->renderState || !ref->renderState->niNode) continue;
        if (!ref->baseForm || ref->baseForm->typeID != kFormDoor || !rtti_is(ref->baseForm, ".?AVTESObjectDOOR@@")) continue;
        out.push_back({ref, {ref->pos[0], ref->pos[1], ref->pos[2]}, ref->rot[2], teleports(ref)});
    }
    return out;
}

bool activate(fnv::TESObjectREFR *ref) { return reinterpret_cast<Activate>(0x00573170)(ref, fnv::player(), 0, 0, 1); }

}
