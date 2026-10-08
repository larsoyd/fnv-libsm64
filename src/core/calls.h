#pragma once
#include <cstdint>

namespace sm64nv {

// where the five byte relative call at site goes, 0 when the bytes are no call
uint32_t call_target(uint32_t site, const uint8_t code[5]);
// the five bytes of a call at site that goes to
void call_code(uint32_t site, uint32_t to, uint8_t code[5]);

}
