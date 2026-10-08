#include "mio0.h"

#include <cstring>

namespace sm64nv {

namespace {

const size_t kLargestBlock = 4 << 20;

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

}

std::vector<uint8_t> mio0_unpack(std::span<const uint8_t> rom, size_t block, size_t need) {
    if (rom.size() < block + 16 || memcmp(&rom[block], "MIO0", 4)) return {};
    const uint8_t *src = &rom[block];
    size_t left = rom.size() - block, size = be32(src + 4), pairs = be32(src + 8), plain = be32(src + 12), bits = 16;
    if (size < need || size > kLargestBlock || pairs < 16 || plain < pairs || plain > left) return {};
    const size_t bits_end = pairs, pairs_end = plain;
    std::vector<uint8_t> out;
    out.reserve(need + 18);
    for (uint8_t flags = 0, bit = 0; out.size() < need; flags <<= 1, bit--) {
        if (!bit) {
            if (bits >= bits_end) return {};
            flags = src[bits++], bit = 8;
        }
        if (flags & 0x80) {
            if (plain >= left) return {};
            out.push_back(src[plain++]);
            continue;
        }
        // a run copied from what is already out, how far back and how long in two bytes
        if (pairs + 2 > pairs_end) return {};
        size_t n = (src[pairs] >> 4) + 3, back = ((src[pairs] & 15) << 8 | src[pairs + 1]) + 1;
        pairs += 2;
        if (back > out.size()) return {};
        while (n--) out.push_back(out[out.size() - back]);
    }
    return out;
}

}
