#pragma once
#include "core/pad.h"

#include <string>

namespace sm64nv {

struct ControlState {
    uint8_t controls;
    bool noclip, third, hidden;
    float zoom, chase;
    bool foreground, active;
};

// false when the game's input globals are missing or report no keyboard
bool read_game_pad(Pad &pad, bool &toggle, bool &activate);
// saves keep the courier's state, false when the slot is not the game's own
bool hook_player_save(std::string &why);
// how far the camera stick is pushed to the right, -1 to 1
float look_stick();
// the game's own pad in use flag, then whether its menus are on keys and mouse
std::string pad_mode();
// while mario has the player the game is handed the pad without his buttons
bool hook_pad(std::string &why);
// stops the game clicking at an activate key it cannot act on while mario plays
// false when the call it replaces is not the game's own
bool hook_activate_sound(std::string &why);
// how many of those clicks were kept quiet since the last call
int take_hushed();
// turns off the game's own movement and goes third person with the body hidden
bool take_player(const ControlState &courier, std::string &why);
// puts back what take_player changed from the state it saw before
// looking the game switched off meanwhile goes off again, and saved is told so
void release_player(ControlState &saved);
// a door gives the player his controls and view back, a scene may switch looking off
// what the control flags were when something had to be put right, -1 when nothing had
int hold_player();
void move_player(Vec3 pos);
// the node the courier's body hangs under, a room indoors where there are rooms
void *body_parent();
// culls the parts of the courier's body, true when any of them was showing
bool hide_body();
// keys only reach the game while its window has focus so ask the window manager for it
void focus_game();
ControlState control_state();
std::string describe(const ControlState &cs);
// world position of the scene camera, false when it is not a camera
bool camera_pos(Vec3 &out);

}
