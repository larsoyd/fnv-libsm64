#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sm64nv {

// the first need bytes of a mio0 block, empty when it is not one or its data runs out
std::vector<uint8_t> mio0_unpack(std::span<const uint8_t> rom, size_t block, size_t need);

}
