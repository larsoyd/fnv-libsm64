#include "hud.h"
#include "mio0.h"

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

}
