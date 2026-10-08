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

}
