#pragma once
#include "core/hud.h"

#include <string>
#include <vector>

namespace sm64nv {

// a quad to draw over the finished frame, with the picture it shows, -1 for none
struct OverlayQuad {
    HudQuad q;
    int picture = -1;
};

struct OverlayStats {
    unsigned presents = 0, drawn = 0, captured = 0;
    int width = 0, height = 0;
};

// draws over each frame the game shows, false when its d3d9 device is not found
bool overlay_hook(std::string &why);
// a picture the overlay can draw, rgba rows from the top, its index
int overlay_picture(std::vector<uint8_t> rgba, int width, int height);
// what to draw from the next frame on
void overlay_set(std::vector<OverlayQuad> quads);
// the size of the frames the game presents, 0 before the first
void overlay_size(int &width, int &height);
// writes the next frame with what is drawn over it beside the game's own screenshots
void overlay_capture(const std::string &path);
OverlayStats overlay_take_stats();

}
