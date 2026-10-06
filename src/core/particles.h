#pragma once
#include "frame.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

// the effects mario asks for, bits of his particle flags
inline constexpr uint32_t kPuffDust = 1 << 0, kPuffWallStars = 1 << 1, kPuffGroundStars = 1 << 4, kPuffRing = 1 << 16,
                          kPuffHit = 1 << 18;

enum class Puff : uint8_t { mist, smoke, star, shard };

// sm64 units and ticks, as the original effects are written
struct Particle {
    Vec3 pos, prev, vel;
    float size, full, alpha;
    Puff model;
    uint8_t motion;
    int age, life;
};

class Particles {
public:
    static constexpr size_t kMax = 256;

    explicit Particles(uint32_t seed = 1) : random_(seed) {}
    // one tick: what is alive ages and moves, then mario's flags of this tick add theirs
    void step(uint32_t flags, Vec3 pos, float yaw);
    // an effect raised outside his own tick
    void emit(uint32_t flags, Vec3 pos, float yaw);
    const std::vector<Particle> &alive() const { return alive_; }
    void clear() { alive_.clear(); }

private:
    Particle *add(Puff model, uint8_t motion, int life, Vec3 pos);
    float rand();

    std::vector<Particle> alive_;
    uint32_t random_;
};

}
