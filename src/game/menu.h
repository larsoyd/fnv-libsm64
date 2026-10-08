#pragma once
#include "core/config.h"

namespace sm64nv {

// the game's own message box with a button for each option and one to close it
bool show_options(const Config &c);
// the button pressed since the box was shown, -1 while none was
int take_options_pick();

}
