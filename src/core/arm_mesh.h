#pragma once
#include "core/frame.h"

#include <cstdint>
#include <span>

namespace sm64nv {

struct ArmVertex { Vec3 position, normal; };
struct ArmPart {
    const char *bone;
    std::span<const ArmVertex> vertices;
    std::span<const uint16_t> indices;
    Vec3 color;
};

}
