#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sm64nv {

std::string sha256_hex(const std::vector<uint8_t> &data);

}
