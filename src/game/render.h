#pragma once
#include "core/mesh.h"

#include <string>

namespace sm64nv {

// mario as engine triangle shapes under parent, the face on a second one with the atlas
// made once, later calls show them again and refuse another parent
bool mario_mesh_create(void *parent, const char *texture, std::string &why);
// the node every loaded cell hangs under, it stays through cell changes
void *scene_root();
void mario_mesh_update(const MeshOut &body, const MeshOut &decal, Vec3 world);
void mario_mesh_hide();
bool mario_mesh_hidden();
void take_screenshot();
bool menu_mode();

}
