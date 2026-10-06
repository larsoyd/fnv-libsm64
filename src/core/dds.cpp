#include "dds.h"
#include "mesh.h"

#include <cstring>

namespace sm64nv {

std::vector<uint8_t> atlas_dds(const uint8_t *rgba) {
    const uint32_t w = kAtlasWidth, h = SM64_TEXTURE_HEIGHT;
    std::vector<uint8_t> d(128 + 4 * w * h, 0);
    auto put = [&](size_t off, uint32_t v) { memcpy(&d[off], &v, 4); };
    memcpy(d.data(), "DDS ", 4);
    put(4, 124), put(8, 0x100F), put(12, h), put(16, w), put(20, 4 * w);
    put(76, 32), put(80, 0x41), put(88, 32);
    put(92, 0x00FF0000), put(96, 0x0000FF00), put(100, 0x000000FF), put(104, 0xFF000000), put(108, 0x1000);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < (uint32_t)SM64_TEXTURE_WIDTH; x++) {
            const uint8_t *s = &rgba[4 * (y * SM64_TEXTURE_WIDTH + x)];
            uint8_t *o = &d[128 + 4 * (y * w + x)];
            o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = s[3];
        }
    return d;
}

}
