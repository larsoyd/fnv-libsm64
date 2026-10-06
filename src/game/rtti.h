#pragma once

namespace sm64nv {

// msvc rtti of the game exe, empty name for anything that is not a game object
const char *rtti_name(const void *obj);
bool rtti_is(const void *obj, const char *type_name);
// true for every class built on NiNode, the ones that carry no msvc rtti included
bool is_ni_node(const void *obj);

}
