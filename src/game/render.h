#pragma once
#include "core/mesh.h"

#include <string>

namespace sm64nv {

// mario as an engine triangle shape under parent, a node with an identity world rotation
bool mario_mesh_create(void *parent, std::string &why);
void mario_mesh_update(const MeshOut &m, Vec3 world);
void take_screenshot();
bool menu_mode();

}
