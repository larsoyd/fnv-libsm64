#pragma once
#include "core/mesh.h"

#include <string>

namespace sm64nv {

// mario as engine triangle shapes under parent, a node with an identity world rotation
// the face goes on a second shape textured from the atlas
bool mario_mesh_create(void *parent, const char *texture, std::string &why);
void mario_mesh_update(const MeshOut &body, const MeshOut &decal, Vec3 world);
void mario_mesh_hide();
bool mario_mesh_hidden();
void take_screenshot();
bool menu_mode();

}
