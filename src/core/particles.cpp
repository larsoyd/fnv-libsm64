#include "particles.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace sm64nv {

static const float kPi = 3.14159265f;
enum Motion : uint8_t { kStill, kMist, kRing, kStar, kShard };

namespace {

// a spot beside, above and ahead of mario as he is turned
Vec3 ahead(Vec3 pos, float yaw, float up, float forward) {
    return {pos.x + std::sin(yaw) * forward, pos.y + up, pos.z + std::cos(yaw) * forward};
}

Vec3 thrown(float yaw, float forward, float up) { return {std::sin(yaw) * forward, up, std::cos(yaw) * forward}; }

// sm64 angles, a full turn is 65536
float turn(int units) { return units * kPi / 32768; }

}

float Particles::rand() {
    random_ ^= random_ << 13, random_ ^= random_ >> 17, random_ ^= random_ << 5;
    return (random_ >> 8) * (1.0f / 16777216);
}

Particle *Particles::add(Puff model, uint8_t motion, int life, Vec3 pos) {
    if (alive_.size() >= kMax) return nullptr;
    alive_.push_back({pos, pos, {}, 1, 1, 1, model, motion, 0, life});
    return &alive_.back();
}

void Particles::emit(uint32_t flags, Vec3 pos, float yaw) {
    auto scatter = [&](Particle *p) {
        if (p) p->pos.x += (rand() - 0.5f) * 40, p->pos.z += (rand() - 0.5f) * 40, p->prev = p->pos;
    };
    if (flags & kPuffDust) {
        // the mist is gone before its sixth tick is drawn, the smoke plays seven pictures
        Particle *mist = add(Puff::mist, kMist, 5, {pos.x, pos.y + 30, pos.z});
        scatter(mist);
        if (mist) mist->alpha = 50 / 255.0f, mist->size = 0.1f;
        scatter(add(Puff::smoke, kStill, 7, pos));
    }
    if (flags & kPuffRing)
        for (int i = 0; i < 20; i++) {
            Particle *p = add(Puff::mist, kRing, 20, {pos.x, pos.y + 20, pos.z});
            if (!p) break;
            float way = rand() * 2 * kPi, speed = 10 + rand() * 5;
            p->vel = thrown(way, speed, 0), p->full = 3 + rand() * 0.15f, p->size = 0, p->alpha = 254 / 255.0f;
        }
    if (flags & kPuffGroundStars)
        for (int i = 0; i < 8; i++)
            if (Particle *p = add(Puff::star, kStar, 10, {pos.x, pos.y - 20, pos.z})) p->vel = thrown(i * kPi / 4, 25, 14), p->size = p->full = 0.28f;
    // both fans leave a spot in front of him and come back past him
    static const std::pair<int, int> kStarFan[] = {{-8192, 0}, {0, 0}, {8192, 0}, {-5734, 5734}, {5734, 5734}, {-5734, -5734}, {5734, -5734}};
    static const std::pair<int, int> kShardFan[] = {{-12288, 0}, {12288, 0}, {-8601, 8601}, {8601, 8601}, {-8601, -8601}, {8601, -8601}};
    auto fan = [&](std::span<const std::pair<int, int>> ways, Puff model, uint8_t motion, int life, Vec3 from, float size) {
        for (auto [side, rise] : ways)
            if (Particle *p = add(model, motion, life, from))
                p->vel = thrown(yaw + kPi + turn(side), std::cos(turn(rise)) * 25, std::sin(turn(rise)) * 25), p->size = p->full = size;
    };
    if (flags & kPuffWallStars) fan(kStarFan, Puff::star, kStar, 10, ahead(pos, yaw, 30, 110), 0.28f);
    if (flags & kPuffHit) fan(kShardFan, Puff::shard, kShard, 7, ahead(pos, yaw, 60, 100), 1.28f);
}

void Particles::step(uint32_t flags, Vec3 pos, float yaw) {
    for (Particle &p : alive_) {
        p.prev = p.pos, p.age++;
        if (p.motion == kMist) p.size = 0.1f + p.age * 0.5f;
        if (p.motion == kStar) p.size = std::fmax(0.0f, p.full - 0.015f * p.age);
        if (p.motion == kShard) p.size = std::fmax(0.0f, p.full - 0.2f * p.age);
        if (p.motion == kRing) {
            p.alpha = std::fmax(0.0f, (254 - 13 * p.age) / 255.0f), p.size = p.full * (1 - p.alpha);
            // sm64 drags them by the square of their speed
            for (float *v : {&p.vel.x, &p.vel.z}) {
                float drag = *v * *v * 0.003f;
                *v = std::fabs(*v) <= drag ? 0 : *v - std::copysign(drag, *v);
            }
        }
        p.pos = {p.pos.x + p.vel.x, p.pos.y + p.vel.y, p.pos.z + p.vel.z};
    }
    std::erase_if(alive_, [](const Particle &p) { return p.age >= p.life; });
    emit(flags, pos, yaw);
}

}
