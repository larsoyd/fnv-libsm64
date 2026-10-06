#include "game/collision.h"
#include "core/hull.h"
#include "game/rtti.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace sm64nv {

namespace {

// node and body positions agree on this ratio to five digits, plain 7 is off by 1 in 8000
const float kHavokToGame = 1 / 0.142875f;
const int kMaxHullPoints = 64;
// static, anim static, transparent, trees, props, terrain and ground
const uint32_t kSolidLayers = 1u << 1 | 1u << 2 | 1u << 3 | 1u << 9 | 1u << 10 | 1u << 13 | 1u << 17;

template <typename T> T at(const void *base, size_t off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + off);
}

Vec3 vec_at(const void *base, size_t off) { return {at<float>(base, off), at<float>(base, off + 4), at<float>(base, off + 8)}; }

struct Xf {
    Vec3 c0, c1, c2, t;
};

Vec3 rotate(const Xf &x, Vec3 p) {
    return {x.c0.x * p.x + x.c1.x * p.y + x.c2.x * p.z, x.c0.y * p.x + x.c1.y * p.y + x.c2.y * p.z,
            x.c0.z * p.x + x.c1.z * p.y + x.c2.z * p.z};
}

Vec3 apply(const Xf &x, Vec3 p) {
    Vec3 r = rotate(x, p);
    return {r.x + x.t.x, r.y + x.t.y, r.z + x.t.z};
}

Xf compose(const Xf &outer, const Xf &inner) {
    return {rotate(outer, inner.c0), rotate(outer, inner.c1), rotate(outer, inner.c2), apply(outer, inner.t)};
}

// hkTransform is three rotation columns then the translation, each a four float vector
Xf hk_transform(const void *p) { return {vec_at(p, 0), vec_at(p, 16), vec_at(p, 32), vec_at(p, 48)}; }

struct Walker {
    Vec3 lo, hi;
    std::vector<Tri> &out;
    CollisionStats &stats;
    uint32_t owner = 0;

    bool inside(Vec3 p) const { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y && p.z >= lo.z && p.z <= hi.z; }

    static Vec3 to_game(const Xf &xf, Vec3 p) {
        p = apply(xf, p);
        return {p.x * kHavokToGame, p.y * kHavokToGame, p.z * kHavokToGame};
    }

    void keep(Vec3 a, Vec3 b, Vec3 c) {
        if (inside(a) || inside(b) || inside(c)) out.push_back({a, b, c, owner});
    }

    void emit(const Xf &xf, Vec3 a, Vec3 b, Vec3 c) { keep(to_game(xf, a), to_game(xf, b), to_game(xf, c)); }

    void emit_all(const Xf &xf, const std::vector<Tri> &tris) {
        for (const Tri &t : tris) emit(xf, t.a, t.b, t.c);
    }

    void packed_strips(const void *s, const Xf &xf) {
        const void *data = at<const void *>(s, 0x84);
        Vec3 scale = vec_at(s, 0x90);
        uint32_t ntri = at<uint32_t>(data, 0x08), nvert = at<uint32_t>(data, 0x0C);
        const uint16_t *tris = at<const uint16_t *>(data, 0x14);
        const float *verts = at<const float *>(data, 0x18);
        std::vector<Vec3> v;
        for (uint32_t i = 0; i < nvert; i++)
            v.push_back(to_game(xf, {verts[i * 3] * scale.x, verts[i * 3 + 1] * scale.y, verts[i * 3 + 2] * scale.z}));
        for (uint32_t i = 0; i < ntri; i++) {
            const uint16_t *t = tris + i * 4;
            if (t[0] < nvert && t[1] < nvert && t[2] < nvert) keep(v[t[0]], v[t[1]], v[t[2]]);
        }
    }

    void convex_vertices(const void *s, const Xf &xf) {
        const float *blocks = at<const float *>(s, 0x40);
        int nblocks = at<int>(s, 0x44), nverts = at<int>(s, 0x4C);
        if (nverts > kMaxHullPoints || nblocks != (nverts + 3) / 4) {
            stats.skipped_types[".?AVhkpConvexVerticesShape@@ big"]++;
            return;
        }
        std::vector<Vec3> pts;
        for (int i = 0; i < nverts; i++) {
            const float *b = blocks + (i / 4) * 12;
            pts.push_back({b[i % 4], b[4 + i % 4], b[8 + i % 4]});
        }
        emit_all(xf, convex_hull(pts));
    }

    void shape(const void *s, const Xf &xf, int depth) {
        const char *name = rtti_name(s);
        if (depth > 8 || !*name) {
            stats.skipped_types[*name ? name : "unknown"]++;
            return;
        }
        // scaled mopp keeps its child in the same slot and the scale sits on the strips
        if (rtti_is(s, ".?AVhkpMoppBvTreeShape@@")) shape(at<const void *>(s, 0x34), xf, depth + 1);
        else if (!strcmp(name, ".?AVhkPackedNiTriStripsShape@@")) packed_strips(s, xf);
        else if (!strcmp(name, ".?AVhkpListShape@@")) {
            const uint8_t *items = at<const uint8_t *>(s, 0x18);
            for (int i = 0, n = at<int>(s, 0x1C); i < n; i++) shape(at<const void *>(items, i * 16), xf, depth + 1);
        } else if (!strcmp(name, ".?AVhkpTransformShape@@"))
            shape(at<const void *>(s, 0x14), compose(xf, hk_transform(static_cast<const uint8_t *>(s) + 0x30)), depth + 1);
        else if (!strcmp(name, ".?AVhkpConvexTransformShape@@"))
            shape(at<const void *>(s, 0x18), compose(xf, hk_transform(static_cast<const uint8_t *>(s) + 0x20)), depth + 1);
        else if (!strcmp(name, ".?AVhkpConvexTranslateShape@@")) {
            Xf moved = xf;
            moved.t = apply(xf, vec_at(s, 0x20));
            shape(at<const void *>(s, 0x18), moved, depth + 1);
        } else if (!strcmp(name, ".?AVhkpBoxShape@@")) {
            float r = at<float>(s, 0x10);
            Vec3 h = vec_at(s, 0x20);
            emit_all(xf, box_tris({0, 0, 0}, {h.x + r, h.y + r, h.z + r}));
        } else if (!strcmp(name, ".?AVhkpConvexVerticesShape@@")) convex_vertices(s, xf);
        else stats.skipped_types[name]++;
    }

    void body(const void *collision_object) {
        if (!rtti_is(collision_object, ".?AVbhkNiCollisionObject@@")) return;
        const void *world_obj = at<const void *>(collision_object, 0x10);
        if (!rtti_is(world_obj, ".?AVbhkRigidBody@@")) return;
        const void *hk = at<const void *>(world_obj, 0x08);
        if (!rtti_is(hk, ".?AVhkpRigidBody@@")) return;
        int layer = at<uint8_t>(hk, 0x2C) & 0x7F;
        if (!(kSolidLayers >> layer & 1)) {
            stats.skipped_layers[layer]++;
            return;
        }
        const void *motion = at<const void *>(hk, 0x18);
        if (!motion) return;
        Xf xf = hk_transform(motion);
        stats.bodies++;
        if (!strcmp(rtti_name(world_obj), ".?AVbhkRigidBody@@")) {
            Vec3 node_t = vec_at(at<const void *>(collision_object, 0x08), 0x8C);
            float err = std::fabs(node_t.x - xf.t.x * kHavokToGame) + std::fabs(node_t.y - xf.t.y * kHavokToGame) +
                        std::fabs(node_t.z - xf.t.z * kHavokToGame);
            stats.scale_samples++;
            if (err > stats.scale_max_err) {
                stats.scale_max_err = err;
                stats.scale_worst_node = node_t;
                stats.scale_worst_body = {xf.t.x * kHavokToGame, xf.t.y * kHavokToGame, xf.t.z * kHavokToGame};
            }
        }
        shape(at<const void *>(hk, 0x10), xf, 0);
    }

    void node(const void *av, int depth) {
        if (depth > 32) return;
        if (const void *co = at<const void *>(av, 0x1C)) body(co);
        if (!rtti_is(av, ".?AVNiNode@@")) return;
        const void *const *children = at<const void *const *>(av, 0xA0);
        for (int i = 0, n = at<uint16_t>(av, 0xA6); i < n; i++)
            if (children[i]) node(children[i], depth + 1);
    }
};

}

std::vector<Tri> gather_collision(fnv::TESObjectCELL *cell, Vec3 c, float r, CollisionStats &stats) {
    std::vector<Tri> out;
    Walker w{{c.x - r, c.y - r, c.z - r}, {c.x + r, c.y + r, c.z + r}, out, stats};
    for (auto *it = &cell->objectList; it; it = it->next) {
        fnv::TESObjectREFR *ref = it->data;
        if (!ref || !ref->renderState || !ref->renderState->niNode) continue;
        if (fnv::vtbl_of(ref) == fnv::kVtblPlayerCharacter) continue;
        stats.refs++;
        w.owner = ref->form.refID;
        w.node(ref->renderState->niNode, 0);
    }
    return out;
}

void write_obj(const char *path, const std::vector<Tri> &tris) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (const Tri &t : tris)
        fprintf(f, "v %.2f %.2f %.2f\nv %.2f %.2f %.2f\nv %.2f %.2f %.2f\n", t.a.x, t.a.y, t.a.z, t.b.x, t.b.y, t.b.z,
                t.c.x, t.c.y, t.c.z);
    for (size_t i = 0; i < tris.size(); i++) fprintf(f, "f %zu %zu %zu\n", i * 3 + 1, i * 3 + 2, i * 3 + 3);
    fclose(f);
}

}
