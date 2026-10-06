#pragma once
#include "core/agree.h"
#include "core/regather.h"
#include "core/surfaces.h"
#include "game/fnv.h"

#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace sm64nv {

struct CollisionStats {
    int cells = 0, refs = 0, bodies = 0, keyframed = 0;
    // triangles read before the box kept some, and strips skipped by their bounds
    size_t decoded = 0, culled = 0;
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

// walks the loaded cells for the static havok collision within the box around center
// a little at a time, references then ground, under a time budget per step
class Gatherer {
public:
    void begin(fnv::TESObjectCELL *cell, Vec3 center, float radius);
    // true once every reference and every cell's ground has been walked
    bool step(double budget_s);
    bool walking() const { return walking_; }
    std::vector<Tri> take();
    CollisionStats stats;
    int steps = 0;

private:
    fnv::TESObjectCELL *cell_ = nullptr;
    Vec3 lo_{}, hi_{};
    std::vector<Tri> out_;
    std::unordered_set<uint32_t> done_;
    std::set<const fnv::TESObjectCELL *> landed_;
    bool walking_ = false;
};

// the whole walk in one go
std::vector<Tri> gather_collision(fnv::TESObjectCELL *cell, Vec3 center, float radius, CollisionStats &stats);
// the cell itself indoors, outdoors every cell that has its references attached
std::vector<fnv::TESObjectCELL *> loaded_cells(fnv::TESObjectCELL *cell);
// references with a shape in the cell or outdoors in every loaded one, cheap to ask often
int loaded_refs(fnv::TESObjectCELL *cell);
void write_obj(const char *path, const std::vector<Tri> &tris);
// where every node under this one stands, folded so the key changes when any of them moves
uint64_t node_pose(const void *node);

}
