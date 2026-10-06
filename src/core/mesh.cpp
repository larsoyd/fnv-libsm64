#include "mesh.h"

#include <algorithm>
#include <cmath>

namespace sm64nv {

static void clear(MeshOut &out) {
    const size_t n = 3 * SM64_GEO_MAX_TRIANGLES;
    out.pos.assign(n, {0, 0, 0});
    out.normal.assign(n, {0, 0, 1});
    out.color.assign(4 * n, 0);
    out.uv.assign(2 * n, 0);
    out.tris = 0;
}

static Vec3 unit(Vec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / len, v.y / len, v.z / len};
}

static float shade(Vec3 n, Vec3 l) { return kAmbient + (1 - kAmbient) * std::max(0.0f, n.x * l.x + n.y * l.y + n.z * l.z); }

static Vec3 vertex(const Frame &f, const SM64MarioGeometryBuffers &g, size_t i) {
    return to_game(f, {g.position[i * 3], g.position[i * 3 + 1], g.position[i * 3 + 2]});
}

static Vec3 normal(const SM64MarioGeometryBuffers &g, size_t i) {
    return dir_to_game({g.normal[i * 3], g.normal[i * 3 + 1], g.normal[i * 3 + 2]});
}

void convert_mesh(const Frame &f, const SM64MarioGeometryBuffers &g, Vec3 anchor, Vec3 to_light, MeshOut &out) {
    clear(out);
    out.tris = g.numTrianglesUsed;
    Vec3 l = unit(to_light);
    for (size_t i = 0; i < 3 * (size_t)g.numTrianglesUsed; i++) {
        Vec3 p = vertex(f, g, i);
        out.pos[i] = {p.x - anchor.x, p.y - anchor.y, p.z - anchor.z};
        out.normal[i] = normal(g, i);
        float s = shade(out.normal[i], l);
        for (int c = 0; c < 3; c++) out.color[i * 4 + c] = g.color[i * 3 + c] * s;
        out.color[i * 4 + 3] = 1;
        out.uv[i * 2] = g.uv[i * 2];
        out.uv[i * 2 + 1] = g.uv[i * 2 + 1];
    }
}

void convert_decal(const Frame &f, const SM64MarioGeometryBuffers &g, Vec3 anchor, Vec3 to_light, MeshOut &out) {
    clear(out);
    Vec3 l = unit(to_light);
    const float k = (float)SM64_TEXTURE_WIDTH / kAtlasWidth;
    size_t o = 0;
    for (size_t t = 0; t < g.numTrianglesUsed; t++) {
        const float *uv = &g.uv[t * 6];
        if (std::all_of(uv, uv + 6, [](float v) { return v == 1.0f; })) continue;
        for (size_t i = 3 * t; i < 3 * t + 3; i++, o++) {
            Vec3 p = vertex(f, g, i), n = normal(g, i);
            out.pos[o] = {p.x - anchor.x, p.y - anchor.y, p.z - anchor.z};
            out.normal[o] = n;
            float s = shade(n, l);
            out.color[o * 4] = out.color[o * 4 + 1] = out.color[o * 4 + 2] = s;
            out.color[o * 4 + 3] = 1;
            out.uv[o * 2] = g.uv[i * 2] * k;
            out.uv[o * 2 + 1] = g.uv[i * 2 + 1];
        }
        out.tris++;
    }
}

}
