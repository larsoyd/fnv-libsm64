#include "game/render.h"
#include "game/log.h"
#include "game/rtti.h"

#include <cmath>
#include <cstring>

namespace sm64nv {

namespace {

const uint32_t kVerts = 3 * SM64_GEO_MAX_TRIANGLES;

using NiAlloc = void *(__cdecl *)(size_t);
using TriShapeDataCtor = void *(__thiscall *)(void *, uint32_t, void *, void *, void *, void *, uint32_t, uint32_t,
                                               uint32_t, void *);
using TriShapeCtor = void *(__thiscall *)(void *, void *);
using ObjCtor = void *(__fastcall *)(void *);
using ListNodeAlloc = void **(__cdecl *)();
using ShaderSetup = void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t);
using AddObject = void(__thiscall *)(void *, void *, bool);
using UpdateDownward = void(__thiscall *)(void *, const void *, uint32_t);
using Screenshot = void(__cdecl *)(int);
using MenuMode = uint8_t(__cdecl *)();

template <typename F> F engine(uintptr_t addr) { return reinterpret_cast<F>(addr); }
template <typename F> F virt(void *obj, size_t off) { return reinterpret_cast<F>((*static_cast<uintptr_t **>(obj))[off / 4]); }
template <typename T> T &field(void *obj, size_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(obj) + off); }

const uint8_t kUpdateData[12] = {};
void *g_shape, *g_data, *g_parent;
Vec3 *g_verts, *g_normals;
float *g_colors;

void *ni_alloc(size_t n) { return engine<NiAlloc>(0xAA13E0)(n); }

void add_property(void *geom, void *prop) {
    field<uint32_t>(prop, 0x04)++;
    void **node = engine<ListNodeAlloc>(0x43A010)();
    node[2] = prop;
    field<uint32_t>(geom, 0x2C)++;
    void **head = field<void **>(geom, 0x24);
    field<void **>(geom, 0x24) = node;
    if (head) node[0] = head, head[1] = node;
    else field<void **>(geom, 0x28) = node;
}

}

bool mario_mesh_create(void *parent, std::string &why) {
    if (!rtti_is(parent, ".?AVNiNode@@")) {
        why = std::string("parent type=") + rtti_name(parent);
        return false;
    }
    if (g_shape && parent == g_parent) {
        field<uint32_t>(g_shape, 0x30) &= ~1u;
        logf("mesh shown parent=%s", rtti_name(parent));
        return true;
    }
    g_verts = static_cast<Vec3 *>(ni_alloc(kVerts * sizeof(Vec3)));
    g_normals = static_cast<Vec3 *>(ni_alloc(kVerts * sizeof(Vec3)));
    g_colors = static_cast<float *>(ni_alloc(kVerts * 4 * sizeof(float)));
    auto *tris = static_cast<uint16_t *>(ni_alloc(kVerts * sizeof(uint16_t)));
    for (uint32_t i = 0; i < kVerts; i++) tris[i] = (uint16_t)i, g_verts[i] = {0, 0, 0}, g_normals[i] = {0, 0, 1};
    memset(g_colors, 0, kVerts * 4 * sizeof(float));
    g_data = engine<TriShapeDataCtor>(0xA7B630)(ni_alloc(0x58), kVerts, g_verts, g_normals, g_colors, nullptr, 0, 0,
                                                SM64_GEO_MAX_TRIANGLES, tris);
    g_shape = engine<TriShapeCtor>(0xA74480)(ni_alloc(0xC4), g_data);
    add_property(g_shape, engine<ObjCtor>(0xB6FC90)(ni_alloc(0x80)));
    engine<ObjCtor>(0xA5A040)(g_shape);
    engine<ShaderSetup>(0xB57BD0)(g_shape, 0, 0, 0);
    virt<AddObject>(parent, 0xDC)(parent, g_shape, true);
    g_parent = parent;
    float *rot = &field<float>(parent, 0x68);
    logf("mesh created parent=%s parent_scale=%.3f parent_rot_diag=%.3f,%.3f,%.3f shape=%s", rtti_name(parent),
         field<float>(parent, 0x98), rot[0], rot[4], rot[8], rtti_name(g_shape));
    return true;
}

void mario_mesh_update(const MeshOut &m, Vec3 world) {
    memcpy(g_verts, m.pos.data(), kVerts * sizeof(Vec3));
    memcpy(g_normals, m.normal.data(), kVerts * sizeof(Vec3));
    memcpy(g_colors, m.color.data(), kVerts * 4 * sizeof(float));
    field<uint16_t>(g_data, 0x0E) |= 0x0F;
    // bound center and radius, mario stands about 110 units tall
    field<float>(g_data, 0x10) = 0, field<float>(g_data, 0x14) = 0, field<float>(g_data, 0x18) = 60;
    field<float>(g_data, 0x1C) = 120;
    Vec3 p = {world.x - field<float>(g_parent, 0x8C), world.y - field<float>(g_parent, 0x90),
              world.z - field<float>(g_parent, 0x94)};
    field<float>(g_shape, 0x58) = p.x, field<float>(g_shape, 0x5C) = p.y, field<float>(g_shape, 0x60) = p.z;
    virt<UpdateDownward>(g_shape, 0xA4)(g_shape, kUpdateData, 0);
}

void mario_mesh_hide() { field<uint32_t>(g_shape, 0x30) |= 1; }

bool mario_mesh_hidden() { return field<uint32_t>(g_shape, 0x30) & 1; }

void take_screenshot() { engine<Screenshot>(0x878860)(0); }

bool menu_mode() { return engine<MenuMode>(0x702360)() != 0; }

}
