#pragma once
#include "core/surfaces.h"
#include "game/fnv.h"

#include <map>
#include <string>
#include <vector>

namespace sm64nv {

struct CollisionStats {
    int refs = 0, bodies = 0;
    std::map<std::string, int> skipped_types;
    std::map<int, int> skipped_layers;
    int scale_samples = 0;
    float scale_max_err = 0;
    Vec3 scale_worst_node{}, scale_worst_body{};
};

// static havok collision of the cell in game units, kept to the box around center
std::vector<Tri> gather_collision(fnv::TESObjectCELL *cell, Vec3 center, float radius, CollisionStats &stats);
void write_obj(const char *path, const std::vector<Tri> &tris);

}
