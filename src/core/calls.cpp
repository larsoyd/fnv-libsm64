#include "core/calls.h"

#include <cstring>

namespace sm64nv {

const uint8_t kCall = 0xE8;

uint32_t call_target(uint32_t site, const uint8_t code[5]) {
    if (code[0] != kCall) return 0;
    int32_t rel;
    memcpy(&rel, code + 1, 4);
    return site + 5 + (uint32_t)rel;
}

void call_code(uint32_t site, uint32_t to, uint8_t code[5]) {
    uint32_t rel = to - (site + 5);
    code[0] = kCall;
    memcpy(code + 1, &rel, 4);
}

}
