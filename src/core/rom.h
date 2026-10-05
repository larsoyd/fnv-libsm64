#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sm64nv {

inline constexpr const char *kUsRomSha256 = "17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91";
inline constexpr size_t kUsRomSize = 8388608;

struct RomCheck {
    bool ok;
    std::string reason;
    std::string sha256;
};

RomCheck check_rom(const std::vector<uint8_t> &rom);
std::vector<uint8_t> read_file(const std::string &path);

}
