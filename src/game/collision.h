#pragma once
#include "core/agree.h"
#include "core/surfaces.h"
#include "game/fnv.h"

#include <map>
#include <string>
#include <vector>

namespace sm64nv {

struct CollisionStats {
    int cells = 0, refs = 0, bodies = 0, keyframed = 0;
    // quarters of a cell's ground and how far the worst sits from where it is drawn
    int land_quads = 0;
    float land_max_err = 0;
    std::map<std::string, int> skipped_types;
    std::map<int, int> skipped_layers;
    // each body against its node, in game units and in the largest gap of a rotation entry
    Agreement scale{0.5f}, turn{0.05f};
    uint32_t scale_worst_owner = 0, turn_worst_owner = 0;
    Vec3 scale_worst_node{}, scale_worst_body{};
};

// static havok collision in game units, kept to the box around center
// outdoors it covers every loaded cell and the ground
std::vector<Tri> gather_collision(fnv::TESObjectCELL *cell, Vec3 center, float radius, CollisionStats &stats);
// the cell itself indoors, outdoors every cell that has its references attached
std::vector<fnv::TESObjectCELL *> loaded_cells(fnv::TESObjectCELL *cell);
// references with a shape in the cell or outdoors in every loaded one, cheap to ask often
int loaded_refs(fnv::TESObjectCELL *cell);
void write_obj(const char *path, const std::vector<Tri> &tris);

}
