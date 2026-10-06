#pragma once
#include "mesh.h"
#include "particles.h"

#include <span>
#include <vector>

namespace sm64nv {

inline constexpr int kPuffAtlasWidth = 512, kPuffCell = 32;

// the mist and the seven pictures of walk smoke out of the rom, then a cell of plain white
// rgba rows from the top, empty when the rom does not hold them where they should be
std::vector<uint8_t> puff_atlas(std::span<const uint8_t> rom);

struct PuffVertex {
    // sm64 units across and up from the particle
    float x, y;
    // into the atlas, 0 to 1
    float u, v;
    float color[4];
};

// the triangles of a model wound counter clockwise, frame picks the picture of the smoke
std::vector<PuffVertex> puff_model(Puff model, int frame);

// what is alive as triangles that face the eye, each drawn between its last two ticks
// game units around anchor like mario's own mesh, what the mesh cannot hold is left out
void build_puffs(const Frame &f, const std::vector<Particle> &alive, float blend, Vec3 eye, Vec3 anchor, MeshOut &out);

}
