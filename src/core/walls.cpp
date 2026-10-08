#include "walls.h"
#include "surfaces.h"

#include <algorithm>
#include <cmath>
#include <utility>

extern "C" {
SM64SurfaceCollisionData *loaded_surface_iter_get_at_index(uint32_t group, uint32_t index);
// the library's own wall lookup, which the build renames to make room for the one below
int32_t sm64_front_wall_collisions(SM64WallCollisionData *data);
// and its floor lookup, renamed the same way
float sm64_floor_under(float x, float y, float z, SM64SurfaceCollisionData **floor);
}

namespace sm64nv {

namespace {

std::vector<bool> g_two_sided;
struct Tread {
    SM64Surface surface;
    int32_t lo[3], hi[3];
};
std::vector<Tread> g_treads;
std::vector<SM64SurfaceCollisionData *> g_risers;

bool tread_at(float x, float z, float top) {
    float height;
    x = int32_t(x), z = int32_t(z);
    for (const auto &t : g_treads) {
        if (x < t.lo[0] || x > t.hi[0] || z < t.lo[2] || z > t.hi[2] || top < t.lo[1] - 2 || top > t.hi[1] + 2) continue;
        if (height_at(t.surface, x, z, height) && std::fabs(height - top) <= 2) return true;
    }
    return false;
}

std::vector<SM64SurfaceCollisionData *> stair_risers(const SM64WallCollisionData &d) {
    const float kStepHeight = 78;
    std::vector<SM64SurfaceCollisionData *> risers;
    // small probes still need to hit the riser for punches
    if (d.radius < 24) return risers;
    for (auto *surface : g_risers) {
        auto &s = *surface;
        if (!s.isValid) continue;
        float y = d.y + d.offsetY;
        if (y < s.lowerY || y > s.upperY) continue;
        if (d.x < std::min({s.vertex1[0], s.vertex2[0], s.vertex3[0]}) - d.radius ||
            d.x > std::max({s.vertex1[0], s.vertex2[0], s.vertex3[0]}) + d.radius ||
            d.z < std::min({s.vertex1[2], s.vertex2[2], s.vertex3[2]}) - d.radius ||
            d.z > std::max({s.vertex1[2], s.vertex2[2], s.vertex3[2]}) + d.radius) continue;
        float top = std::max({s.vertex1[1], s.vertex2[1], s.vertex3[1]});
        float bottom = std::min({s.vertex1[1], s.vertex2[1], s.vertex3[1]});
        if (top <= d.y || top > d.y + 2 * kStepHeight) continue;
        if (top > d.y + kStepHeight && (top - bottom > kStepHeight || bottom > d.y + kStepHeight)) continue;
        float offset = s.normal.x * d.x + s.normal.y * d.y + s.normal.z * d.z + s.originOffset;
        if (std::fabs(offset) > d.radius) continue;
        bool tread = false, approach = top <= d.y + kStepHeight;
        // a quarter step can already be across the face before its floor is resolved
        for (float side : {-2.0f, 2.0f}) {
            float across = offset + side;
            float x = d.x - s.normal.x * across, z = d.z - s.normal.z * across;
            if (!approach && tread_at(x, z, bottom)) approach = true;
            if (!tread && tread_at(x, z, top)) tread = true;
        }
        if (!tread || !approach) continue;
        s.isValid = false;
        risers.push_back(&s);
    }
    return risers;
}

// loose faces turn toward mario while the far side of a solid waits for a second pass
std::vector<SM64SurfaceCollisionData *> face_walls(float x, float y, float z, float radius) {
    std::vector<SM64SurfaceCollisionData *> backs;
    for (uint32_t i = 0; i < g_two_sided.size(); i++) {
        SM64SurfaceCollisionData &s = *loaded_surface_iter_get_at_index(0, i);
        if (!s.isValid || std::fabs(s.normal.y) > 0.01f || y < s.lowerY || y > s.upperY) continue;
        float offset = s.normal.x * x + s.normal.y * y + s.normal.z * z + s.originOffset;
        if (offset >= 0 || offset < -radius) continue;
        if (!g_two_sided[i]) {
            s.isValid = false;
            backs.push_back(&s);
            continue;
        }
        std::swap(s.vertex2, s.vertex3);
        s.normal = {-s.normal.x, -s.normal.y, -s.normal.z};
        s.originOffset = -s.originOffset;
    }
    return backs;
}

}

void load_surfaces(const std::vector<SM64Surface> &surfaces, std::vector<bool> two_sided) {
    two_sided.resize(surfaces.size());
    g_two_sided = std::move(two_sided);
    sm64_static_surfaces_load(surfaces.data(), (uint32_t)surfaces.size());
    g_treads.clear(), g_risers.clear();
    // treads can face either way while the window catches up with quarter steps
    for (uint32_t i = 0; i < surfaces.size(); i++) {
        auto &s = *loaded_surface_iter_get_at_index(0, i);
        if (!s.isValid) continue;
        if (std::fabs(s.normal.y) >= 0.8f) {
            Tread t{surfaces[i], {}, {}};
            for (int axis = 0; axis < 3; axis++) {
                t.lo[axis] = std::min({s.vertex1[axis], s.vertex2[axis], s.vertex3[axis]});
                t.hi[axis] = std::max({s.vertex1[axis], s.vertex2[axis], s.vertex3[axis]});
            }
            g_treads.push_back(t);
        } else if (std::fabs(s.normal.y) <= 0.01f) g_risers.push_back(&s);
    }
}

void load_window(const SurfaceWindow &w) {
    std::vector<bool> two_sided(w.loaded().size());
    for (size_t i = 0; i < two_sided.size(); i++) two_sided[i] = !w.fixed(i);
    load_surfaces(w.loaded(), std::move(two_sided));
}

}

extern "C" int32_t find_wall_collisions(SM64WallCollisionData *d) {
    auto risers = sm64nv::stair_risers(*d);
    // the library caps the radius the same way
    auto backs = sm64nv::face_walls(d->x, d->y + d->offsetY, d->z, std::min(d->radius, 200.0f));
    int32_t hits = sm64_front_wall_collisions(d);
    for (auto *s : backs) s->isValid = true;
    // keep the escape path when mario starts inside a solid
    if (!hits && !backs.empty()) hits = sm64_front_wall_collisions(d);
    for (auto *s : risers) s->isValid = true;
    return hits;
}

extern "C" int32_t f32_find_wall_collision(float *x, float *y, float *z, float offset_y, float radius) {
    SM64WallCollisionData d{};
    d.x = *x, d.y = *y, d.z = *z, d.offsetY = offset_y, d.radius = radius;
    int32_t hits = find_wall_collisions(&d);
    *x = d.x, *y = d.y, *z = d.z;
    return hits;
}

// looked up from the floor, a ceiling with no top over it holds him at any height above it
extern "C" float vec3f_find_ceil(float *pos, float floor, SM64SurfaceCollisionData **ceil) {
    return sm64_surface_find_ceil(pos[0], std::max(floor + 80.0f, pos[1] + 78.0f), pos[2], ceil);
}

// room pieces leave strips without floor in doorways, which sm64 takes for walls
// floors found close by on two opposite sides, well above what lies under, carry over
extern "C" float find_floor(float x, float y, float z, SM64SurfaceCollisionData **floor) {
    // sm64 units: how far and in what steps a side is looked for, how level the two must be
    const float kReach = 24, kStep = 8, kLevel = 30;
    float under = sm64_floor_under(x, y, z, floor);
    if (*floor && under > y - kLevel) return under;
    struct Side {
        SM64SurfaceCollisionData *floor = nullptr;
        float height = 0, away = 0;
    };
    auto side = [&](float dx, float dz) {
        Side s;
        for (s.away = kStep; s.away <= kReach; s.away += kStep) {
            s.height = sm64_floor_under(x + dx * s.away, y, z + dz * s.away, &s.floor);
            if (s.floor && s.height > under + kLevel) return s;
        }
        return Side{};
    };
    Side a, b;
    float width = 4 * kReach, carried = under;
    for (auto [dx, dz] : {std::pair{1.0f, 0.0f}, {0.0f, 1.0f}, {0.7071f, 0.7071f}, {0.7071f, -0.7071f}}) {
        Side p = side(dx, dz), q = side(-dx, -dz);
        if (!p.floor || !q.floor || std::fabs(p.height - q.height) > kLevel || p.away + q.away >= width) continue;
        a = p, b = q, width = p.away + q.away;
        carried = q.height + (p.height - q.height) * q.away / width;
    }
    if (a.floor) *floor = a.away <= b.away ? a.floor : b.floor;
    return carried;
}
