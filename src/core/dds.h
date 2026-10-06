#pragma once
#include <cstdint>
#include <vector>

namespace sm64nv {

// rgba rows as an uncompressed bgra dds, padded on the right out to wide
std::vector<uint8_t> rgba_dds(const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t wide);
// libsm64's atlas widened to a power of two
std::vector<uint8_t> atlas_dds(const uint8_t *rgba);

}
