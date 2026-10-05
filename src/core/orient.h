#pragma once
#include "frame.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sm64nv {

struct Mesh {
    std::vector<Vec3> verts;
    std::vector<std::array<uint32_t, 3>> tris;
};

struct OrientStats {
    uint32_t components = 0, inside = 0, flipped = 0, conflicts = 0;
};

// rewinds triangles so each connected part is consistent and faces the open point
void orient_mesh(Mesh &m, Vec3 open_point, OrientStats &st);

}
