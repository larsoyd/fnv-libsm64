#include "regather.h"

#include <cmath>

namespace sm64nv {

float Regather::moved(Vec3 at) const { return std::hypot(at.x - center_.x, at.y - center_.y, at.z - center_.z); }

bool Regather::due(Vec3 at, int tick) const { return tick >= wait_until_ && moved(at) > move_; }

void Regather::loaded(Vec3 center) { center_ = center, wait_until_ = 0; }

void Regather::refused(int tick) { wait_until_ = tick + kRetryTicks; }

}
