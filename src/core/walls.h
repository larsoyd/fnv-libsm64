#pragma once
#include "window.h"

#include <vector>

namespace sm64nv {

// hands static surfaces to libsm64, walls marked two sided hold mario from either side
void load_surfaces(const std::vector<SM64Surface> &surfaces, std::vector<bool> two_sided = {});
void load_window(const SurfaceWindow &w);

}
