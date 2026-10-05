#include "mesh.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

void convert_mesh(const Frame &f, const SM64MarioGeometryBuffers &g, Vec3 anchor, Vec3 to_light, MeshOut &out) {
    const size_t n = 3 * SM64_GEO_MAX_TRIANGLES;
    out.pos.assign(n, {0, 0, 0});
    out.normal.assign(n, {0, 0, 1});
    out.color.assign(4 * n, 0);
    out.uv.assign(2 * n, 0);
    out.tris = g.numTrianglesUsed;
    float len = std::sqrt(to_light.x * to_light.x + to_light.y * to_light.y + to_light.z * to_light.z);
    Vec3 l = {to_light.x / len, to_light.y / len, to_light.z / len};
    for (size_t i = 0; i < 3 * (size_t)g.numTrianglesUsed; i++) {
        Vec3 p = to_game(f, {g.position[i * 3], g.position[i * 3 + 1], g.position[i * 3 + 2]});
        out.pos[i] = {p.x - anchor.x, p.y - anchor.y, p.z - anchor.z};
        Vec3 nrm = dir_to_game({g.normal[i * 3], g.normal[i * 3 + 1], g.normal[i * 3 + 2]});
        out.normal[i] = nrm;
        float shade = kAmbient + (1 - kAmbient) * std::max(0.0f, nrm.x * l.x + nrm.y * l.y + nrm.z * l.z);
        for (int c = 0; c < 3; c++) out.color[i * 4 + c] = g.color[i * 3 + c] * shade;
        out.color[i * 4 + 3] = 1;
        out.uv[i * 2] = g.uv[i * 2];
        out.uv[i * 2 + 1] = g.uv[i * 2 + 1];
    }
}

}
