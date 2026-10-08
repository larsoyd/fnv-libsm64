#pragma once
#include "core/mesh.h"
#include "core/arm_mesh.h"

#include <string>

namespace sm64nv {

// mario as engine triangle shapes, his face on a second and his dust and stars on a third
// made once and kept, later calls show them again
bool mario_mesh_create(const char *texture, const char *puff_texture, std::string &why);
// hangs them under parent unless they already are, true when they moved there
// rooms joined by portals draw only what hangs in them, so pass where the player's body is
bool mario_mesh_follow(void *parent);
void mario_mesh_update(const MeshOut &body, const MeshOut &decal, const MeshOut &puffs, Vec3 world);
void mario_mesh_hide(bool hidden = true);
// every node from this one to the top of the scene with its type and cull bit
std::string node_chain(void *node);
std::string mario_mesh_chain();
bool mario_mesh_hidden();
void take_screenshot();
bool menu_mode();
// retained bone-local geometry, attached by the caller
void *create_arm_shape(const ArmPart &part);

}
