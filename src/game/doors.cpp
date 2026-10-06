#include "game/doors.h"
#include "game/collision.h"
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
        // the base form's bounds are six shorts, low corner then high corner
        auto *b = reinterpret_cast<const int16_t *>(reinterpret_cast<const uint8_t *>(ref->baseForm) + 0x24);
        float k = ref->scale;
        out.push_back({ref, {ref->pos[0], ref->pos[1], ref->pos[2]}, ref->rot[2], {b[0] * k, b[1] * k, b[2] * k},
                       {b[3] * k, b[4] * k, b[5] * k}, teleports(ref)});
    }
    return out;
}

std::vector<DoorPoses::Pose> door_poses(fnv::TESObjectCELL *cell) {
    std::vector<DoorPoses::Pose> out;
    for (fnv::TESObjectCELL *c : loaded_cells(cell))
        for (const Door &d : cell_doors(c)) out.push_back({d.ref->form.refID, node_pose(d.ref->renderState->niNode)});
    return out;
}

bool activate(fnv::TESObjectREFR *ref) { return reinterpret_cast<Activate>(0x00573170)(ref, fnv::player(), 0, 0, 1); }

}
