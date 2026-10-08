#include "game/actors.h"
#include "game/collision.h"
#include "game/rtti.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm64nv {

namespace {

const uintptr_t kVtblCharacter = 0x01086A6C, kVtblCreature = 0x010870AC;
// only an actor near the player runs the process that can be shoved
const uintptr_t kVtblHighProcess = 0x01087864;
const uint32_t kHealth = 0x10, kUnarmed = 0x2D;
// a hit record is this large and carries its own count at the end
const size_t kHitSize = 0x64;

using Alloc = void *(__cdecl *)(size_t);
using HitCall = void *(__thiscall *)(void *);
using HitStep = void(__thiscall *)(void *, uint32_t);
using TakeHit = void(__thiscall *)(void *, void *, char);
using ActorValue = float(__thiscall *)(void *, uint32_t);
using Shove = void(__thiscall *)(void *, void *, float, float, float, float);
using ShouldAttack = bool(__thiscall *)(void *, void *, char, int *, char);
using LimbGone = bool(__thiscall *)(void *, int);
using IsEssential = bool(__thiscall *)(void *);
using LineOfSight = uint8_t(__thiscall *)(void *, char, void *, char, int *, char);
using Detection = int(__thiscall *)(void *, char, void *, char *, char, char, int, char *);
using CombatTarget = fnv::TESObjectREFR *(__thiscall *)(void *);

template <typename F> F engine(uintptr_t addr) { return reinterpret_cast<F>(addr); }
template <typename T> T &field(void *obj, size_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(obj) + off); }
template <typename F> F virt(void *obj, size_t off) { return reinterpret_cast<F>((*static_cast<uintptr_t **>(obj))[off / 4]); }
// alive and restrained both stand, anything else is on the ground
const uint32_t kAlive = 0, kRestrained = 5;
// game units, a bound outside this is not a body
const float kLeastSize = 4, kMostSize = 2000;
// half of all records carry no bounds, a person is then as wide, deep and tall as the others
const float kPerson[3] = {46, 34, 132};
const uintptr_t kVtblController = 0x010C49C4;
// havok units to game units
const float kHavokToGame = 1 / 0.142875f;

using GetController = void *(__thiscall *)(void *);

template <typename T> T at(const void *base, size_t off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off);
}

bool is_actor(const fnv::TESObjectREFR *ref) {
    uintptr_t vt = fnv::vtbl_of(ref);
    return (vt == kVtblCharacter || vt == kVtblCreature) && ref->baseForm && ref->renderState &&
           ref->renderState->niNode;
}

bool usable(const float size[3]) {
    return std::all_of(size, size + 3, [](float s) { return s >= kLeastSize && s <= kMostSize; });
}

// the capsule physics moves a creature with, as wide as it is deep
bool body_size(fnv::TESObjectREFR *ref, float size[3]) {
    void *process = at<void *>(ref, 0x68);
    if (fnv::vtbl_of(process) != kVtblHighProcess) return false;
    void *c = virt<GetController>(process, 0x28C)(process);
    if (fnv::vtbl_of(c) != kVtblController) return false;
    size[0] = size[1] = 2 * at<float>(c, 0x5F8) * kHavokToGame, size[2] = at<float>(c, 0x564) * kHavokToGame;
    return true;
}

// the base form's bounds are six shorts, low corner then high corner
const char *sized(fnv::TESObjectREFR *ref, ActorBody &b) {
    auto *lo = reinterpret_cast<const int16_t *>(reinterpret_cast<const uint8_t *>(ref->baseForm) + 0x24), *hi = lo + 3;
    float size[3] = {(hi[0] - lo[0]) * ref->scale, (hi[1] - lo[1]) * ref->scale, hi[2] * ref->scale};
    const char *from = "bounds";
    if (!usable(size) && fnv::vtbl_of(ref) == kVtblCharacter) {
        from = "person";
        for (int i = 0; i < 3; i++) size[i] = kPerson[i] * ref->scale;
    } else if (!usable(size)) {
        from = "body";
        if (!body_size(ref, size)) return nullptr;
    }
    if (!usable(size)) return nullptr;
    b.half_width = size[0] / 2, b.half_length = size[1] / 2, b.height = size[2];
    return from;
}

float apart(const LiveActor &a, Vec3 c) { return std::hypot(a.body.feet.x - c.x, a.body.feet.y - c.y, a.body.feet.z - c.z); }

}

int knocked(fnv::TESObjectREFR *actor) {
    void *process = at<void *>(actor, 0x68);
    return fnv::vtbl_of(process) == kVtblHighProcess ? at<uint8_t>(process, 0x13C) : 0;
}

bool is_person(const fnv::TESObjectREFR *actor) { return fnv::vtbl_of(actor) == kVtblCharacter; }

static void *find_node(void *n, const char *name, int depth = 0) {
    if (!n || depth > 32) return nullptr;
    const char *own = field<const char *>(n, 0x08);
    if (own && !strcmp(own, name)) return n;
    if (!is_ni_node(n)) return nullptr;
    for (int i = 0, count = field<uint16_t>(n, 0xA6); i < count; i++)
        if (void *found = find_node(field<void **>(n, 0xA0)[i], name, depth + 1)) return found;
    return nullptr;
}

Skeleton skeleton_of(fnv::TESObjectREFR *actor) {
    Skeleton s;
    s.root = find_node(actor->renderState->niNode, "Bip01");
    if (s.root) memcpy(s.rot, &field<float>(s.root, 0x34), sizeof s.rot);
    return s;
}

void squash_skeleton(const Skeleton &s, float height, float width) {
    if (!s.root) return;
    // rows of the local rotation scale along the parent's axes, the last row is up
    float *rot = &field<float>(s.root, 0x34);
    for (int i = 0; i < 9; i++) rot[i] = s.rot[i] * (i < 6 ? width : height);
}

float head_height(fnv::TESObjectREFR *actor) {
    void *head = find_node(actor->renderState->niNode, "Bip01 Head");
    return head ? field<float>(head, 0x94) - actor->pos[2] : -1;
}

bool pelvis_at(fnv::TESObjectREFR *actor, Vec3 &out) {
    void *pelvis = find_node(actor->renderState->niNode, "Bip01 Pelvis");
    if (pelvis) out = field<Vec3>(pelvis, 0x8C);
    return pelvis != nullptr;
}

bool actor_essential(fnv::TESObjectREFR *actor) { return engine<IsEssential>(0x0087F3D0)(actor); }

bool hostile_to_player(fnv::TESObjectREFR *actor, int &disposition) {
    disposition = 0;
    return engine<ShouldAttack>(0x008B06D0)(actor, fnv::player(), 0, &disposition, 0);
}

Awareness awareness_of_player(fnv::TESObjectREFR *actor) {
    Awareness a;
    void *p = fnv::player();
    a.sees = engine<LineOfSight>(0x0088B880)(actor, 0, p, 1, nullptr, 0);
    a.ray = engine<LineOfSight>(0x0088B880)(actor, 1, p, 1, nullptr, 1);
    char lost = 0, seen = 0;
    a.detect = engine<Detection>(0x008A0D10)(actor, 0, p, &lost, 0, 0, 0, &seen);
    a.lost = lost, a.seen = seen;
    a.target = virt<CombatTarget>(actor, 0x42C)(actor);
    return a;
}

bool actor_standing(const fnv::TESObjectREFR *actor) {
    uint32_t life = at<uint32_t>(actor, 0x108);
    return life == kAlive || life == kRestrained;
}

// the part of an actor that owns its values sits this far in, the slot picks current or base
static float actor_value(fnv::TESObjectREFR *actor, size_t slot, uint32_t code) {
    void *values = reinterpret_cast<uint8_t *>(actor) + 0xA4;
    return virt<ActorValue>(values, slot)(values, code);
}

float actor_health(fnv::TESObjectREFR *actor) { return actor_value(actor, 0x0C, kHealth); }

float actor_max_health(fnv::TESObjectREFR *actor) { return actor_value(actor, 0x04, kHealth); }

float actor_unarmed(fnv::TESObjectREFR *actor) { return current_value(actor, kUnarmed); }

float current_value(fnv::TESObjectREFR *actor, uint32_t code) { return actor_value(actor, 0x0C, code); }

uint32_t limbs_gone(fnv::TESObjectREFR *actor) {
    uint32_t gone = 0;
    for (int limb = 0; limb < 15; limb++)
        if (engine<LimbGone>(0x00573090)(actor, limb)) gone |= 1u << limb;
    return gone;
}

// the hit record's limb and flags, the torso and a fatal blow that blows the limb apart
const int kTorso = 0;
const uint32_t kFatal = 0x10, kExplodeLimb = 0x40;

static float strike_as(fnv::TESObjectREFR *target, float damage, int place, uint32_t flags) {
    void *hit = engine<Alloc>(0x00401000)(kHitSize);
    engine<HitCall>(0x009B4D90)(hit);
    engine<HitCall>(0x0087ADF0)(hit);
    field<void *>(hit, 0x00) = fnv::player(), field<void *>(hit, 0x04) = target;
    field<uint32_t>(hit, 0x0C) = kUnarmed;
    // damage to health, the same before armour, and a weapon in full repair
    field<float>(hit, 0x14) = field<float>(hit, 0x18) = damage, field<float>(hit, 0x34) = 1;
    // the steps the game runs on a hit it built itself: where it lands, armour, limbs, effects
    engine<HitCall>(0x009B7060)(hit);
    engine<HitStep>(0x009B5A30)(hit, 0);
    engine<HitStep>(0x009B6620)(hit, 0);
    engine<HitCall>(0x009B73D0)(hit);
    float dealt = field<float>(hit, 0x14);
    if (place >= 0) field<int>(hit, 0x10) = place;
    field<uint32_t>(hit, 0x58) |= flags;
    engine<TakeHit>(0x0089A760)(target, hit, 0);
    // letting go of the last count frees the record
    engine<HitCall>(0x0087CEA0)(hit);
    return dealt;
}

float strike(fnv::TESObjectREFR *target, float damage) { return strike_as(target, damage, -1, 0); }

float strike_explode(fnv::TESObjectREFR *target) {
    return strike_as(target, actor_health(target) + 1000, kTorso, kFatal | kExplodeLimb);
}

bool shove(fnv::TESObjectREFR *target, Vec3 from, float force) {
    void *process = at<void *>(target, 0x68);
    if (fnv::vtbl_of(process) != kVtblHighProcess) return false;
    virt<Shove>(process, 0x418)(process, target, from.x, from.y, from.z, force);
    return true;
}

std::vector<fnv::TESObjectREFR *> actors_of_base(fnv::TESObjectCELL *cell, uint32_t base) {
    std::vector<fnv::TESObjectREFR *> out;
    for (fnv::TESObjectCELL *one : loaded_cells(cell))
        for (auto *it = &one->objectList; it; it = it->next)
            if (is_actor(it->data) && it->data->baseForm->refID == base) out.push_back(it->data);
    return out;
}

fnv::TESObjectREFR *find_actor(fnv::TESObjectCELL *cell, uint32_t id) {
    for (fnv::TESObjectCELL *one : loaded_cells(cell))
        for (auto *it = &one->objectList; it; it = it->next)
            if (is_actor(it->data) && it->data->form.refID == id) return it->data;
    return nullptr;
}

std::vector<LiveActor> nearby_actors(fnv::TESObjectCELL *cell, Vec3 c, float reach, ActorStats &stats) {
    std::vector<LiveActor> out;
    for (fnv::TESObjectCELL *one : loaded_cells(cell))
        for (auto *it = &one->objectList; it; it = it->next) {
            fnv::TESObjectREFR *ref = it->data;
            if (!is_actor(ref)) continue;
            stats.seen++;
            LiveActor a{ref, {ref->form.refID, {ref->pos[0], ref->pos[1], ref->pos[2]}, ref->rot[2], 0, 0, 0}, nullptr};
            if (apart(a, c) > reach) stats.away++;
            else if (!actor_standing(ref)) stats.down++;
            else if (!(a.sized = sized(ref, a.body))) stats.unsized++;
            else out.push_back(a);
        }
    std::ranges::sort(out, {}, [&](const LiveActor &a) { return apart(a, c); });
    return out;
}

}
