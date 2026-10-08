#include "actors.h"
#include "surfaces.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sm64nv {

static const uint16_t kTerrainStone = 1;
// mario's wall probe reaches 50, a box narrower than that pushes him both ways at once
static const float kLeastHalf = 66;

namespace {

struct Shape {
    std::vector<SM64Surface> out;
    Vec3 middle;

    // wound so the normal libsm64 works out points away from the middle
    void tri(Vec3 a, Vec3 b, Vec3 c) {
        auto corner = [](Vec3 p) { return std::array<int32_t, 3>{(int32_t)std::lround(p.x), (int32_t)std::lround(p.y), (int32_t)std::lround(p.z)}; };
        SM64Surface s{0, 0, kTerrainStone, {}};
        Vec3 pts[3] = {a, b, c};
        for (int i = 0; i < 3; i++) std::ranges::copy(corner(pts[i]), s.vertices[i]);
        Vec3 n = surface_normal(s);
        Vec3 from{(a.x + b.x + c.x) / 3 - middle.x, (a.y + b.y + c.y) / 3 - middle.y, (a.z + b.z + c.z) / 3 - middle.z};
        if (n.x * from.x + n.y * from.y + n.z * from.z < 0) std::swap(s.vertices[1], s.vertices[2]);
        out.push_back(s);
    }

    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { tri(a, b, c), tri(a, c, d); }
};

SM64ObjectTransform placing(const Frame &f, const ActorBody &a) {
    Vec3 s = to_sm64(f, a.feet);
    return {{s.x, s.y, s.z}, {0, a.heading * 180 / 3.14159265f, 0}};
}

}

ActorBody actor_box(const ActorBody &a, float scale) {
    ActorBody b = a;
    b.half_width = std::max(a.half_width, kLeastHalf / scale), b.half_length = std::max(a.half_length, kLeastHalf / scale);
    return b;
}

float actor_ridge(const ActorBody &box) { return std::min({box.half_width, box.half_length, box.height}); }

std::vector<SM64Surface> actor_surfaces(float half_x, float half_z, float height) {
    float hx = std::max(half_x, kLeastHalf), hz = std::max(half_z, kLeastHalf);
    // the top comes in by the narrower half all round, so it rises at 45 degrees or more
    float in = std::min(hx, hz), rim = height - std::min(in, height);
    Shape s{{}, {0, height / 4, 0}};
    Vec3 foot[4] = {{-hx, 0, -hz}, {hx, 0, -hz}, {hx, 0, hz}, {-hx, 0, hz}};
    auto ridge = [&](Vec3 p) { return Vec3{std::copysign(hx - in, p.x), height, std::copysign(hz - in, p.z)}; };
    for (int i = 0; i < 4; i++) {
        Vec3 a = foot[i], b = foot[(i + 1) % 4], ra = ridge(a), rb = ridge(b);
        if (rim > 0) s.quad(a, b, {b.x, rim, b.z}, {a.x, rim, a.z});
        s.tri({a.x, rim, a.z}, {b.x, rim, b.z}, rb);
        if (ra.x != rb.x || ra.z != rb.z) s.tri({a.x, rim, a.z}, rb, ra);
    }
    s.quad(foot[0], foot[1], foot[2], foot[3]);
    return s.out;
}

void ActorBoxes::sync(const Frame &f, const std::vector<ActorBody> &bodies) {
    auto same = [](const Box &b, const ActorBody &a) {
        return b.id == a.id && b.half_width == a.half_width && b.half_length == a.half_length && b.height == a.height;
    };
    std::erase_if(boxes_, [&](const Box &b) {
        bool gone = std::ranges::none_of(bodies, [&](const ActorBody &a) { return same(b, a); });
        if (gone) sm64_surface_object_delete(b.object);
        return gone;
    });
    for (const ActorBody &a : bodies) {
        SM64ObjectTransform at = placing(f, a);
        auto it = std::ranges::find_if(boxes_, [&](const Box &b) { return b.id == a.id; });
        if (it != boxes_.end()) {
            sm64_surface_object_move(it->object, &at);
        } else if (boxes_.size() < kMax) {
            std::vector<SM64Surface> faces = actor_surfaces(a.half_width * f.scale, a.half_length * f.scale, a.height * f.scale);
            SM64SurfaceObject o{at, (uint32_t)faces.size(), faces.data()};
            boxes_.push_back({a.id, sm64_surface_object_create(&o), a.half_width, a.half_length, a.height});
        }
    }
}

}
