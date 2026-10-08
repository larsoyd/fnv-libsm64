#include "game/pipboy.h"
#include "core/arm_asset.h"
#include "game/fnv.h"
#include "game/log.h"
#include "game/render.h"
#include "game/rtti.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <windows.h>

namespace sm64nv {
namespace {

template <typename T> T &field(void *p, size_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
template <typename F> F virt(void *p, size_t off) { return reinterpret_cast<F>((*static_cast<uintptr_t **>(p))[off / 4]); }
using Free = void(__thiscall *)(void *);
using Attach = void(__thiscall *)(void *, void *, bool);
using Detach = void(__thiscall *)(void *, void *);
using Update = void(__thiscall *)(void *, const void *, uint32_t);

void retain(void *p) { if (p) InterlockedIncrement(&field<LONG>(p, 4)); }
void release(void *p) { if (p && !InterlockedDecrement(&field<LONG>(p, 4))) virt<Free>(p, 4)(p); }

std::array<void *, std::size(kArmParts)> g_shapes{}, g_bones{};
void *g_root;
struct Hidden { void *object; bool was_hidden; };
std::vector<Hidden> g_hidden;

void restore() {
    for (auto h : g_hidden) {
        if (!h.was_hidden) field<uint32_t>(h.object, 0x30) &= ~1u;
        release(h.object);
    }
    g_hidden.clear();
}

bool walk(void *node, std::vector<void *> &nodes, unsigned depth = 0) {
    if (!node) return true;
    if (depth > 64 || nodes.size() >= 2048 || std::find(nodes.begin(), nodes.end(), node) != nodes.end()) return false;
    nodes.push_back(node);
    if (!is_ni_node(node)) return true;
    auto **children = field<void **>(node, 0xA0);
    uint16_t count = field<uint16_t>(node, 0xA6);
    if (count > 2048 || (count && !children)) return false;
    for (unsigned i = 0; i < count; ++i) if (!walk(children[i], nodes, depth + 1)) return false;
    return true;
}

void *find_bone(const std::vector<void *> &nodes, const char *name) {
    void *found = nullptr;
    for (void *p : nodes) {
        const char *n = field<const char *>(p, 8);
        if (!n || strcmp(n, name) || !is_ni_node(p)) continue;
        if (found) return nullptr;
        found = p;
    }
    return found;
}

bool bind(void *root) {
    std::vector<void *> nodes;
    if (!walk(root, nodes)) return false;
    std::array<void *, std::size(kArmParts)> bones{};
    for (size_t i = 0; i < bones.size(); ++i) {
        bones[i] = find_bone(nodes, kArmParts[i].bone);
        if (!bones[i]) return false;
    }
    for (size_t i = 0; i < bones.size(); ++i) {
        if (!g_shapes[i]) g_shapes[i] = create_arm_shape(kArmParts[i]);
        if (!g_shapes[i]) return false;
    }
    retain(root);
    g_root = root, g_bones = bones;
    const uint8_t update[12]{};
    for (size_t i = 0; i < bones.size(); ++i) {
        retain(bones[i]);
        // animated skeletons skip children without selective transform updates
        field<uint32_t>(g_shapes[i], 0x30) |= 0x06;
        virt<Attach>(bones[i], 0xDC)(bones[i], g_shapes[i], true);
        virt<Update>(g_shapes[i], 0xA4)(g_shapes[i], update, 0);
    }
    logf("pipboy arms bound parts=%u", (unsigned)bones.size());
    return true;
}

bool geometry(void *p) { return rtti_is(p, ".?AVNiGeometry@@"); }

bool attached(void *node, void *root);

bool collect(void *biped, void *root, std::vector<void *> &out) {
    // slot roots can share device bones, so only their geometry is hidden
    std::vector<void *> device;
    void *pipboy = field<void *>(biped, 0x2C + 6 * 0x10 + 8);
    if (!pipboy || !walk(pipboy, device)) return false;
    bool wrist = false;
    unsigned depth = 0;
    for (void *p = pipboy; p && p != root && depth++ < 64; p = field<void *>(p, 0x18)) {
        const char *name = field<const char *>(p, 8);
        if (name && (!strcmp(name, "Bip01 L ForeTwist") || !strcmp(name, "Bip01 L Forearm"))) wrist = true;
    }
    if (!wrist || !attached(pipboy, root)) return false;
    for (unsigned slot : {2u, 3u, 4u}) {
        std::vector<void *> nodes;
        if (!walk(field<void *>(biped, 0x2C + slot * 0x10 + 8), nodes)) return false;
        for (void *p : nodes) {
            if (!geometry(p) || std::find(g_shapes.begin(), g_shapes.end(), p) != g_shapes.end()) continue;
            if (!attached(p, root)) return false;
            if (std::find(device.begin(), device.end(), p) != device.end()) return false;
            if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
        }
    }
    return !out.empty();
}

void visibility(bool visible) {
    for (void *p : g_shapes) if (p) {
        if (visible) field<uint32_t>(p, 0x30) &= ~1u;
        else field<uint32_t>(p, 0x30) |= 1u;
    }
}

bool attached(void *node, void *root) {
    for (unsigned depth = 0; node && depth < 64; ++depth, node = field<void *>(node, 0x18))
        if (node == root) return true;
    return false;
}

}

void pipboy_arms_reset() {
    restore();
    visibility(false);
    for (void *p : g_shapes) if (p) {
        if (void *parent = field<void *>(p, 0x18)) virt<Detach>(parent, 0xE8)(parent, p);
    }
    for (void *p : g_bones) release(p);
    release(g_root);
    g_root = nullptr;
    g_bones.fill(nullptr);
}

void pipboy_arms_update(bool mario) {
    if (!mario) return pipboy_arms_reset();
    void *ui = *reinterpret_cast<void **>(0x011D8A80);
    uint32_t mode = ui ? field<uint32_t>(ui, 0x4BC) : 0;
    // states 1..5 cover the request, raise, menu and complete lowering
    if (mode < 1 || mode > 5) {
        restore();
        visibility(false);
        return;
    }
    void *player = fnv::player();
    void *root = player ? field<void *>(player, 0x694) : nullptr;
    void *biped = player ? field<void *>(player, 0x68C) : nullptr;
    if (root != g_root) pipboy_arms_reset();
    if (!is_ni_node(root) || !biped) return pipboy_arms_reset();
    if (g_root && !std::all_of(g_bones.begin(), g_bones.end(), [root](void *p) { return attached(p, root); }))
        pipboy_arms_reset();
    if (g_root) for (size_t i = 0; i < g_shapes.size(); ++i) {
        if (field<void *>(g_shapes[i], 0x18) != g_bones[i]) { pipboy_arms_reset(); break; }
    }
    std::vector<void *> targets;
    if (!collect(biped, root, targets)) {
        restore();
        visibility(false);
        return;
    }
    if (!g_root && !bind(root)) return;
    // release old equipment as soon as its first-person model is replaced
    for (auto it = g_hidden.begin(); it != g_hidden.end();) {
        if (std::find(targets.begin(), targets.end(), it->object) != targets.end()) { ++it; continue; }
        if (!it->was_hidden) field<uint32_t>(it->object, 0x30) &= ~1u;
        release(it->object);
        it = g_hidden.erase(it);
    }
    for (void *p : targets) {
        if (std::none_of(g_hidden.begin(), g_hidden.end(), [p](Hidden h) { return h.object == p; })) {
            retain(p);
            g_hidden.push_back({p, (field<uint32_t>(p, 0x30) & 1) != 0});
        }
        field<uint32_t>(p, 0x30) |= 1u;
    }
    visibility(true);
}

void pipboy_arms_log() {
    logf("pipboy arms status bound=%d hidden=%u parts=%u", g_root != nullptr, (unsigned)g_hidden.size(),
         g_root ? (unsigned)g_shapes.size() : 0);
    if (g_root) {
        float error = 0;
        for (size_t i = 0; i < g_shapes.size(); ++i)
            for (size_t offset = 0x68; offset <= 0x98; offset += 4)
                error = std::fmax(error, std::fabs(field<float>(g_bones[i], offset) - field<float>(g_shapes[i], offset)));
        Vec3 hand = field<Vec3>(g_bones[2], 0x8C), finger = field<Vec3>(g_bones[24], 0x8C);
        logf("pipboy arms pose error=%.6f hand=%.2f,%.2f,%.2f finger=%.2f,%.2f,%.2f", error,
             hand.x, hand.y, hand.z, finger.x, finger.y, finger.z);
    }
}

}
