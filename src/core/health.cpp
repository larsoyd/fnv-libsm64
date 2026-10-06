#include "health.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

HealthChange HealthWatch::tick(float health, bool alive) {
    HealthChange c;
    bool living = alive && health > 0;
    // what is lost under zero is nothing to him, he is dead already
    if (primed_ && alive_ && health < last_) c.lost = last_ - std::max(health, 0.0f);
    c.died = primed_ && alive_ && !living;
    primed_ = true, alive_ = living, last_ = health;
    return c;
}

uint16_t health_meter(float health, float max) {
    int wedges = max > 0 ? (int)std::ceil(8 * health / max) : 1;
    return 0x80 + 0x100 * std::clamp(wedges, 1, 8);
}

}
