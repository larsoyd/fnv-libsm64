#include "game/render.h"
#include "game/fnv.h"
#include "game/log.h"
#include "game/rtti.h"

#include <cmath>
#include <cstring>

namespace sm64nv {

namespace {

const uint32_t kVerts = 3 * SM64_GEO_MAX_TRIANGLES;
// blend by source alpha over the body
const uint16_t kDecalAlphaFlags = 0x10ED;

using NiAlloc = void *(__cdecl *)(size_t);
using TriShapeDataCtor = void *(__thiscall *)(void *, uint32_t, void *, void *, void *, void *, uint32_t, uint32_t,
                                               uint32_t, void *);
using TriShapeCtor = void *(__thiscall *)(void *, void *);
using ObjCtor = void *(__fastcall *)(void *);
using AlphaCtor = void *(__cdecl *)();
using ListNodeAlloc = void **(__cdecl *)();
using ShaderSetup = void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t);
using AddObject = void(__thiscall *)(void *, void *, bool);
using UpdateDownward = void(__thiscall *)(void *, const void *, uint32_t);
using LoadTexture = void(__thiscall *)(void *, const char *, void **, bool, bool);
using Screenshot = void(__cdecl *)(int);
using MenuMode = uint8_t(__cdecl *)();

template <typename F> F engine(uintptr_t addr) { return reinterpret_cast<F>(addr); }
template <typename F> F virt(void *obj, size_t off) { return reinterpret_cast<F>((*static_cast<uintptr_t **>(obj))[off / 4]); }
template <typename T> T &field(void *obj, size_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(obj) + off); }

struct Shape {
    void *shape, *data;
    Vec3 *verts, *normals;
    float *colors, *uv;
};

const uint8_t kUpdateData[12] = {};
Shape g_body, g_decal;
void *g_parent, *g_face_tex;

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

Shape make_shape(void *texture) {
    Shape s{};
    s.verts = static_cast<Vec3 *>(ni_alloc(kVerts * sizeof(Vec3)));
    s.normals = static_cast<Vec3 *>(ni_alloc(kVerts * sizeof(Vec3)));
    s.colors = static_cast<float *>(ni_alloc(kVerts * 4 * sizeof(float)));
    if (texture) s.uv = static_cast<float *>(ni_alloc(kVerts * 2 * sizeof(float)));
    auto *tris = static_cast<uint16_t *>(ni_alloc(kVerts * sizeof(uint16_t)));
    for (uint32_t i = 0; i < kVerts; i++) tris[i] = (uint16_t)i, s.verts[i] = {0, 0, 0}, s.normals[i] = {0, 0, 1};
    memset(s.colors, 0, kVerts * 4 * sizeof(float));
    if (s.uv) memset(s.uv, 0, kVerts * 2 * sizeof(float));
    s.data = engine<TriShapeDataCtor>(0xA7B630)(ni_alloc(0x58), kVerts, s.verts, s.normals, s.colors, s.uv, s.uv ? 1 : 0,
                                                0, SM64_GEO_MAX_TRIANGLES, tris);
    s.shape = engine<TriShapeCtor>(0xA74480)(ni_alloc(0xC4), s.data);
    void *prop = engine<ObjCtor>(0xB6FC90)(ni_alloc(0x80));
    if (texture) {
        field<void *>(prop, 0x60) = texture;
        field<uint32_t>(texture, 0x04)++;
        void *alpha = engine<AlphaCtor>(0xA5CEB0)();
        field<uint16_t>(alpha, 0x18) = kDecalAlphaFlags;
        add_property(s.shape, alpha);
    }
    add_property(s.shape, prop);
    engine<ObjCtor>(0xA5A040)(s.shape);
    engine<ShaderSetup>(0xB57BD0)(s.shape, 0, 0, 0);
    return s;
}

void attach(Shape &s, void *parent) { virt<AddObject>(parent, 0xDC)(parent, s.shape, true); }

void update_shape(Shape &s, const MeshOut &m, Vec3 at) {
    memcpy(s.verts, m.pos.data(), kVerts * sizeof(Vec3));
    memcpy(s.normals, m.normal.data(), kVerts * sizeof(Vec3));
    memcpy(s.colors, m.color.data(), kVerts * 4 * sizeof(float));
    if (s.uv) memcpy(s.uv, m.uv.data(), kVerts * 2 * sizeof(float));
    field<uint16_t>(s.data, 0x0E) |= 0x0F;
    // bound center and radius, mario stands about 110 units tall
    field<float>(s.data, 0x10) = 0, field<float>(s.data, 0x14) = 0, field<float>(s.data, 0x18) = 60;
    field<float>(s.data, 0x1C) = 120;
    field<float>(s.shape, 0x58) = at.x, field<float>(s.shape, 0x5C) = at.y, field<float>(s.shape, 0x60) = at.z;
    virt<UpdateDownward>(s.shape, 0xA4)(s.shape, kUpdateData, 0);
}

bool load_texture(const char *path, std::string &why) {
    void *tex = nullptr;
    engine<LoadTexture>(0x4568C0)(*reinterpret_cast<void **>(fnv::kTES), path, &tex, true, false);
    if (!rtti_is(tex, ".?AVNiSourceTexture@@")) {
        why = std::string("type=") + rtti_name(tex) + " path=" + path;
        return false;
    }
    g_face_tex = tex;
    logf("texture loaded path=%s refs=%u", path, field<uint32_t>(tex, 0x04));
    return true;
}

void set_hidden(void *shape, bool hidden) {
    if (hidden) field<uint32_t>(shape, 0x30) |= 1;
    else field<uint32_t>(shape, 0x30) &= ~1u;
}

}

bool mario_mesh_create(void *parent, const char *texture, std::string &why) {
    if (!rtti_is(parent, ".?AVNiNode@@")) {
        why = std::string("parent type=") + rtti_name(parent);
        return false;
    }
    if (!g_face_tex && !load_texture(texture, why)) return false;
    if (g_body.shape && (g_parent != parent || field<void *>(g_body.shape, 0x18) != parent)) {
        why = "parent changed";
        return false;
    }
    if (g_body.shape) {
        set_hidden(g_body.shape, false), set_hidden(g_decal.shape, false);
        logf("mesh shown parent=%s", rtti_name(parent));
        return true;
    }
    g_body = make_shape(nullptr);
    g_decal = make_shape(g_face_tex);
    attach(g_body, parent), attach(g_decal, parent);
    g_parent = parent;
    float *rot = &field<float>(parent, 0x68);
    logf("mesh created parent=%s parent_scale=%.3f parent_rot_diag=%.3f,%.3f,%.3f shape=%s decal=%s", rtti_name(parent),
         field<float>(parent, 0x98), rot[0], rot[4], rot[8], rtti_name(g_body.shape), rtti_name(g_decal.shape));
    return true;
}

void mario_mesh_update(const MeshOut &body, const MeshOut &decal, Vec3 world) {
    Vec3 at = {world.x - field<float>(g_parent, 0x8C), world.y - field<float>(g_parent, 0x90),
               world.z - field<float>(g_parent, 0x94)};
    update_shape(g_body, body, at);
    update_shape(g_decal, decal, at);
}

void *scene_root() { return field<void *>(*reinterpret_cast<void **>(fnv::kTES), 0x0C); }

void mario_mesh_hide() {
    if (g_body.shape) set_hidden(g_body.shape, true), set_hidden(g_decal.shape, true);
}

bool mario_mesh_hidden() { return (field<uint32_t>(g_body.shape, 0x30) & 1) && (field<uint32_t>(g_decal.shape, 0x30) & 1); }

void take_screenshot() { engine<Screenshot>(0x878860)(0); }

bool menu_mode() { return engine<MenuMode>(0x702360)() != 0; }

}
