#pragma once
#include <cstdint>
#include <vector>

namespace sm64nv {

// libsm64's rgba atlas widened to a power of two as an uncompressed bgra dds
std::vector<uint8_t> atlas_dds(const uint8_t *rgba);

}
