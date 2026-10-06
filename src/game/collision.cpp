#include "game/collision.h"
#include "core/hull.h"
#include "core/land.h"
#include "game/rtti.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

namespace sm64nv {

namespace {

// node and body positions agree on this ratio to five digits, plain 7 is off by 1 in 8000
const float kHavokToGame = 1 / 0.142875f;
// the transform a body points at sits this far into its motion
const int kMotionState = 0x10;
const int kMaxHullPoints = 64;
const float kCellSize = 4096;
// each quarter of a cell's ground is this many points a side
const int kLandPoints = 17;
const uint32_t kMaxGrid = 11;
// a cell in this state has all its references attached
const uint8_t kCellAttached = 6;
// static, anim static, transparent, trees, props, terrain and ground
// then small transparent, which is chain link fence, and its animated kind for the gates
const uint32_t kSolidLayers = 1u << 1 | 1u << 2 | 1u << 3 | 1u << 9 | 1u << 10 | 1u << 13 | 1u << 17 | 1u << 26 | 1u << 28;

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

// a node keeps its world rotation as three rows, then its place in game units
Xf node_transform(const void *node) {
    Vec3 r0 = vec_at(node, 0x68), r1 = vec_at(node, 0x74), r2 = vec_at(node, 0x80), t = vec_at(node, 0x8C);
    return {{r0.x, r1.x, r2.x}, {r0.y, r1.y, r2.y}, {r0.z, r1.z, r2.z}, {t.x / kHavokToGame, t.y / kHavokToGame, t.z / kHavokToGame}};
}

float turn_error(const Xf &a, const Xf &b) {
    float err = 0;
    for (auto [p, q] : {std::pair{a.c0, b.c0}, {a.c1, b.c1}, {a.c2, b.c2}})
        err = std::max({err, std::fabs(p.x - q.x), std::fabs(p.y - q.y), std::fabs(p.z - q.z)});
    return err;
}

// a reference with a shape in the scene that is not the player
bool shown(const fnv::TESObjectREFR *ref) {
    return ref && ref->renderState && ref->renderState->niNode && fnv::vtbl_of(ref) != fnv::kVtblPlayerCharacter;
}

struct Walker {
    Vec3 lo, hi;
    std::vector<Tri> &out;
    CollisionStats &stats;
    uint32_t owner = 0;

    static Vec3 to_game(const Xf &xf, Vec3 p) {
        p = apply(xf, p);
        return {p.x * kHavokToGame, p.y * kHavokToGame, p.z * kHavokToGame};
    }

    void keep(Vec3 a, Vec3 b, Vec3 c, bool solid = false) {
        Tri t{a, b, c, owner, solid};
        if (reaches(t, lo, hi)) out.push_back(t);
    }

    void emit_all(const Xf &xf, const std::vector<Tri> &tris) {
        for (const Tri &t : tris) keep(to_game(xf, t.a), to_game(xf, t.b), to_game(xf, t.c), t.solid);
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
        const void *node = at<const void *>(collision_object, 0x08);
        bool plain = !strcmp(rtti_name(world_obj), ".?AVbhkRigidBody@@");
        // havok leaves a keyframed body where it was while collision is off, its node still moves
        // fixed motion is built on the keyframed class, so the name has to match exactly
        if (plain && !strcmp(rtti_name(static_cast<const uint8_t *>(motion) - kMotionState), ".?AVhkpKeyframedRigidMotion@@")) {
            xf = node_transform(node);
            stats.keyframed++;
        } else if (plain) {
            Vec3 node_t = vec_at(node, 0x8C);
            float err = std::fabs(node_t.x - xf.t.x * kHavokToGame) + std::fabs(node_t.y - xf.t.y * kHavokToGame) +
                        std::fabs(node_t.z - xf.t.z * kHavokToGame);
            if (stats.turn.add(turn_error(node_transform(node), xf))) stats.turn_worst_owner = owner;
            if (stats.scale.add(err)) {
                stats.scale_worst_owner = owner;
                stats.scale_worst_node = node_t;
                stats.scale_worst_body = {xf.t.x * kHavokToGame, xf.t.y * kHavokToGame, xf.t.z * kHavokToGame};
            }
        }
        shape(at<const void *>(hk, 0x10), xf, 0);
    }

    // how far a quarter is from where its drawn shape sits and from the layout expected of it
    float land_error(const void *node, const Vec3 *points, int q, Vec3 off) const {
        const void *shape = rtti_is(node, ".?AVNiNode@@") && at<uint16_t>(node, 0xA6) ? *at<const void *const *>(node, 0xA0) : nullptr;
        if (!shape) return kCellSize;
        Vec3 t = vec_at(shape, 0x8C), first = points[0], last = points[kLandPoints * kLandPoints - 1];
        float x = q % 2 ? 0 : -kCellSize / 2, y = q / 2 ? 0 : -kCellSize / 2;
        return std::fabs(t.x - off.x) + std::fabs(t.y - off.y) + std::fabs(t.z - off.z) + std::fabs(first.x - x) +
               std::fabs(first.y - y) + std::fabs(last.x - x - kCellSize / 2) + std::fabs(last.y - y - kCellSize / 2);
    }

    void land(const fnv::TESObjectCELL *cell) {
        const void *data = rtti_is(cell->land, ".?AVTESObjectLAND@@") ? at<const void *>(cell->land, 0x28) : nullptr;
        const void *geometry = data ? at<const void *>(data, 0x04) : nullptr;
        const void *const *nodes = data ? at<const void *const *>(data, 0x00) : nullptr;
        if (!geometry || !nodes || !cell->coords) return;
        // points are measured from the middle of the cell at the mean height
        Vec3 off{(cell->coords[0] + 0.5f) * kCellSize, (cell->coords[1] + 0.5f) * kCellSize, at<float>(data, 0xA0)};
        float half = kCellSize / 2;
        if (off.x + half < lo.x || off.x - half > hi.x || off.y + half < lo.y || off.y - half > hi.y) return;
        owner = at<uint32_t>(cell->land, 0x0C);
        for (int q = 0; q < 4; q++) {
            const Vec3 *points = at<const Vec3 *>(geometry, q * 4);
            if (!points) continue;
            stats.land_quads++;
            stats.land_max_err = std::fmax(stats.land_max_err, land_error(nodes[q], points, q, off));
            for (const Tri &t : land_tris(points, kLandPoints, off, owner)) keep(t.a, t.b, t.c, true);
        }
    }

    void refs(fnv::TESObjectCELL *cell) {
        stats.cells++;
        for (auto *it = &cell->objectList; it; it = it->next) {
            fnv::TESObjectREFR *ref = it->data;
            if (!shown(ref)) continue;
            stats.refs++;
            owner = ref->form.refID;
            node(ref->renderState->niNode, 0);
        }
    }

    void node(const void *av, int depth) {
        if (depth > 32) return;
        if (const void *co = at<const void *>(av, 0x1C)) body(co);
        // the root of a placed model has no rtti, and its children hold most of the collision
        if (!is_ni_node(av)) return;
        const void *const *children = at<const void *const *>(av, 0xA0);
        for (int i = 0, n = at<uint16_t>(av, 0xA6); i < n; i++)
            if (children[i]) node(children[i], depth + 1);
    }
};

}

// the cell itself indoors, outdoors every cell of the grid that has its references attached
template <typename F> void each_cell(fnv::TESObjectCELL *cell, F visit) {
    if (cell->interior()) return visit(cell);
    const void *grid = at<const void *>(*reinterpret_cast<void **>(fnv::kTES), 0x08);
    if (!rtti_is(grid, ".?AVGridCellArray@@")) return;
    auto *cells = at<fnv::TESObjectCELL *const *>(grid, 0x10);
    uint32_t n = std::min(at<uint32_t>(grid, 0x0C), kMaxGrid);
    for (uint32_t i = 0; i < n * n; i++)
        if (fnv::vtbl_of(cells[i]) == fnv::kVtblTESObjectCELL && cells[i]->cellState == kCellAttached) visit(cells[i]);
}

std::vector<Tri> gather_collision(fnv::TESObjectCELL *cell, Vec3 c, float r, CollisionStats &stats) {
    std::vector<Tri> out;
    Walker w{{c.x - r, c.y - r, c.z - r}, {c.x + r, c.y + r, c.z + r}, out, stats};
    each_cell(cell, [&](fnv::TESObjectCELL *one) {
        w.refs(one);
        if (!one->interior()) w.land(one);
    });
    return out;
}

int loaded_refs(fnv::TESObjectCELL *cell) {
    int n = 0;
    each_cell(cell, [&](fnv::TESObjectCELL *one) {
        for (auto *it = &one->objectList; it; it = it->next)
            n += shown(it->data);
    });
    return n;
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
