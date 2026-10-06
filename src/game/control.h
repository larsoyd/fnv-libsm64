#pragma once
#include "core/pad.h"

#include <string>

namespace sm64nv {

struct ControlState {
    uint8_t controls;
    bool noclip, third, hidden;
    float zoom;
    bool foreground, active;
};

// false when the game's input globals are missing or report no keyboard
bool read_game_pad(Pad &pad);
// turns off the game's own movement and goes third person with the body hidden
bool take_player(std::string &why);
void move_player(Vec3 pos);
// keys only reach the game while its window has focus so ask the window manager for it
void focus_game();
ControlState control_state();
// world position of the scene camera, false when it is not a camera
bool camera_pos(Vec3 &out);

}
