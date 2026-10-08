#include "hud.h"
#include "mio0.h"

#include <algorithm>

namespace sm64nv {

namespace {

// the us rom's packed blocks and where the pictures sit once unpacked
const size_t kMeterBlock = 2102288, kMeterLeft = 144352, kMeterRight = 148448, kMeterWedges = 152544, kWedgeBytes = 2048;
const size_t kSegment2 = 1083968, kWipeOffset = 82616;

uint8_t widen5(unsigned v) { return (uint8_t)(v * 255 / 31); }

// one rgba16 texel, five bits a colour and one for solid
void rgba16(const uint8_t *t, uint8_t *out) {
    out[0] = widen5(t[0] >> 3);
    out[1] = widen5((t[0] & 7) << 2 | t[1] >> 6);
    out[2] = widen5((t[1] >> 1) & 31);
    out[3] = t[1] & 1 ? 255 : 0;
}

}

std::vector<uint8_t> power_meter_art(std::span<const uint8_t> rom) {
    std::vector<uint8_t> block = mio0_unpack(rom, kMeterBlock, kMeterWedges + 8 * kWedgeBytes);
    if (block.empty()) return {};
    std::vector<uint8_t> art(4 * kMeterPictures * kHudPicture * kHudPicture);
    for (int p = 0; p < kMeterPictures; p++)
        for (int y = 0; y < kHudPicture; y++)
            for (int x = 0; x < kHudPicture; x++) {
                // the frame is two 32 wide halves, the full wedges of picture p lie over its middle
                const uint8_t *t = &block[(x < 32 ? kMeterLeft : kMeterRight) + 2 * (y * 32 + x % 32)];
                if (p && x >= 16 && x < 48 && y >= 16 && y < 48) {
                    const uint8_t *w = &block[kMeterWedges + (8 - p) * kWedgeBytes + 2 * ((y - 16) * 32 + x - 16)];
                    if (w[1] & 1) t = w;
                }
                rgba16(t, &art[4 * ((p * kHudPicture + y) * kHudPicture + x)]);
            }
    return art;
}

std::vector<uint8_t> bowser_wipe_art(std::span<const uint8_t> rom) {
    std::vector<uint8_t> block = mio0_unpack(rom, kSegment2, kWipeOffset + 32 * 64);
    if (block.empty()) return {};
    std::vector<uint8_t> art(4 * kHudPicture * kHudPicture);
    for (int y = 0; y < kHudPicture; y++)
        for (int x = 0; x < kHudPicture; x++) {
            // the rom holds the right half, the left is drawn mirrored and both meet on column 0
            uint8_t ia = block[kWipeOffset + y * 32 + (x < 32 ? 31 - x : x - 32)];
            uint8_t *o = &art[4 * (y * kHudPicture + x)];
            o[0] = o[1] = o[2] = (ia >> 4) * 17, o[3] = (ia & 15) * 17;
        }
    return art;
}

// sm64's meter comes in at 166, rests at 200, stays 45 ticks once full and is gone past 300
const int kMeterIn = 166, kMeterRest = 200, kMeterStay = 45, kMeterGone = 300, kMeterX = 140;

bool PowerMeter::tick(int wedges, bool hurt) {
    bool rose = wedges > wedges_;
    wedges_ = wedges;
    if (state_ != State::hiding) {
        if ((wedges < 8 || hurt) && state_ == State::hidden) state_ = State::emphasized, y_ = kMeterIn;
        if (hurt) timer_ = 0;
        // sm64 refills a wedge at a time and starts the wait at seven, a stimpak can skip it
        if (wedges == 8 && stored_ < 8) timer_ = 0;
        if (wedges == 8 && timer_ > kMeterStay) state_ = State::hiding;
        stored_ = wedges;
    }
    if (state_ == State::hidden) return rose;
    if (state_ == State::emphasized && timer_ == kMeterStay) state_ = State::deemphasizing;
    if (state_ == State::deemphasizing) {
        y_ += y_ > 195 ? 1 : y_ > 190 ? 2 : y_ > 180 ? 3 : 5;
        if (y_ > kMeterRest) y_ = kMeterRest, state_ = State::visible;
    } else if (state_ == State::hiding && (y_ += 20) > kMeterGone) {
        state_ = State::hidden, timer_ = 0;
    }
    timer_++;
    return rose;
}

namespace {

// the n64 screen is 240 high, a 4:3 picture of it fills the height of any screen
float n64_scale(int height) { return height / 240.0f; }

}

HudQuad meter_quad(int y, int wedges, int width, int height) {
    float s = n64_scale(height), cx = width / 2.0f + (kMeterX - 160) * s, cy = (240 - y) * s, half = 32 * s;
    return {cx - half, cy - half, cx + half, cy + half, 0, wedges / 9.0f, 1, (wedges + 1) / 9.0f, true, 0xFFFFFFFF};
}

// sm64's wipe closes from the 4:3 corners to a small square
const int kWipeStart = 320, kWipeEnd = 16;

int wipe_radius(int frame) {
    frame = std::clamp(frame, 0, kWipeFrames - 1);
    float r = kWipeStart + frame * (float)(kWipeEnd - kWipeStart) / (kWipeFrames - 1);
    return (int)(short)(r + 0.5f);
}

std::vector<HudQuad> wipe_quads(int frame, int width, int height) {
    float w = (float)width, h = (float)height;
    if (frame >= kWipeFrames) return {{0, 0, w, h}};
    float r = wipe_radius(frame) * n64_scale(height), x0 = w / 2 - r, x1 = w / 2 + r, y0 = h / 2 - r, y1 = h / 2 + r;
    std::vector<HudQuad> out;
    if (x0 > 0) out.push_back({0, 0, x0, h});
    if (x1 < w) out.push_back({x1, 0, w, h});
    if (y0 > 0) out.push_back({x0, 0, x1, y0});
    if (y1 < h) out.push_back({x0, y1, x1, h});
    // the n64 draws texels -31 to 31 of the mirrored head across the square
    out.push_back({x0, y0, x1, y1, 1 / 64.0f, 0, 63 / 64.0f, 63 / 64.0f, true});
    return out;
}

DeathCue DeathScene::tick(bool alive) {
    if (alive) return ticks_ = -1, DeathCue::none;
    if (ticks_ < 0) return ticks_ = 0, DeathCue::fall;
    return ++ticks_ == warp_ ? DeathCue::laugh : DeathCue::none;
}

// sm64's standing death, death on the back and on the stomach
int death_warp_tick(uint32_t action) {
    switch (action) {
    case 0x00021316: return 54;
    case 0x00021315: return 37;
    default: return 80;
    }
}

}
