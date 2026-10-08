#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace sm64nv {

inline constexpr int kHudPicture = 64, kMeterPictures = 9;

// sm64's power meter, nine pictures from empty to full in a column, rgba rows from the top
// empty when the rom does not hold them where they should be
std::vector<uint8_t> power_meter_art(std::span<const uint8_t> rom);
// bowser's head the death wipe closes on, one picture mirrored about its middle
std::vector<uint8_t> bowser_wipe_art(std::span<const uint8_t> rom);

// sm64's power meter, shown when health falls and gone 45 ticks after it is full again
class PowerMeter {
public:
    // one 30 hz tick with the wedges to show, 1 to 8, true when they went up
    bool tick(int wedges);
    bool shown() const { return state_ != State::hidden; }
    // n64 screen units up from the bottom to the middle of the picture
    int y() const { return y_; }
    int wedges() const { return wedges_; }

private:
    enum class State { hidden, emphasized, deemphasizing, visible, hiding };
    State state_ = State::hidden;
    int y_ = 166, timer_ = 0, wedges_ = 8, stored_ = 8;
};

// a screen rectangle in pixels from the top left, with its part of a picture when textured
struct HudQuad {
    float x0, y0, x1, y1;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    bool textured = false;
    // the colour it is drawn in, the picture's own colours are multiplied by it
    uint32_t argb = 0xFF000000;
};
HudQuad meter_quad(int y, int wedges, int width, int height);

inline constexpr int kWipeFrames = 48;
// how far the wipe's square reaches from the middle at a frame, in n64 units
int wipe_radius(int frame);
// black around bowser's head in a closing square, all black once closed
std::vector<HudQuad> wipe_quads(int frame, int width, int height);

enum class DeathCue { none, fall, laugh };
// mario falls when the courier dies, bowser laughs and the wipe starts later
class DeathScene {
public:
    DeathCue tick(bool alive);
    bool dying() const { return ticks_ >= 0; }
    // the frame of the wipe, -1 before it starts
    int wipe_frame() const { return ticks_ < kLaughTicks ? -1 : ticks_ - kLaughTicks; }

private:
    static constexpr int kLaughTicks = 54;
    int ticks_ = -1;
};

}
