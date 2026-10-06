#pragma once
#include <cstdint>

namespace sm64nv {

// what changed in the courier's health since the last reading
struct HealthChange {
    float lost = 0;
    bool died = false;
};

// follows the game's health and life state, the first reading is only a baseline
class HealthWatch {
public:
    HealthChange tick(float health, bool alive = true);
    bool primed() const { return primed_; }

private:
    bool primed_ = false, alive_ = false;
    float last_ = 0;
};

// sm64's eight wedges for a share of the game's health, never under one while he is alive
uint16_t health_meter(float health, float max);

}
