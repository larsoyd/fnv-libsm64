#include "walls.h"

#include <algorithm>
#include <cmath>
#include <utility>

extern "C" {
SM64SurfaceCollisionData *loaded_surface_iter_get_at_index(uint32_t group, uint32_t index);
// the library's own wall lookup, which the build renames to make room for the one below
int32_t sm64_front_wall_collisions(SM64WallCollisionData *data);
}

namespace sm64nv {

namespace {

std::vector<bool> g_two_sided;

// sm64 walls only hold mario from the front, so one he comes at from behind is turned first
void face_walls(float x, float y, float z, float radius) {
    for (uint32_t i = 0; i < g_two_sided.size(); i++) {
        if (!g_two_sided[i]) continue;
        SM64SurfaceCollisionData &s = *loaded_surface_iter_get_at_index(0, i);
        if (!s.isValid || std::fabs(s.normal.y) > 0.01f || y < s.lowerY || y > s.upperY) continue;
        float offset = s.normal.x * x + s.normal.y * y + s.normal.z * z + s.originOffset;
        if (offset >= 0 || offset < -radius) continue;
        std::swap(s.vertex2, s.vertex3);
        s.normal = {-s.normal.x, -s.normal.y, -s.normal.z};
        s.originOffset = -s.originOffset;
    }
}

}

void load_surfaces(const std::vector<SM64Surface> &surfaces, std::vector<bool> two_sided) {
    two_sided.resize(surfaces.size());
    g_two_sided = std::move(two_sided);
    sm64_static_surfaces_load(surfaces.data(), (uint32_t)surfaces.size());
}

void load_window(const SurfaceWindow &w) {
    std::vector<bool> two_sided(w.loaded().size());
    for (size_t i = 0; i < two_sided.size(); i++) two_sided[i] = !w.fixed(i);
    load_surfaces(w.loaded(), std::move(two_sided));
}

}

extern "C" int32_t find_wall_collisions(SM64WallCollisionData *d) {
    // the library caps the radius the same way
    sm64nv::face_walls(d->x, d->y + d->offsetY, d->z, std::min(d->radius, 200.0f));
    return sm64_front_wall_collisions(d);
}

extern "C" int32_t f32_find_wall_collision(float *x, float *y, float *z, float offset_y, float radius) {
    SM64WallCollisionData d{};
    d.x = *x, d.y = *y, d.z = *z, d.offsetY = offset_y, d.radius = radius;
    int32_t hits = find_wall_collisions(&d);
    *x = d.x, *y = d.y, *z = d.z;
    return hits;
}
