#include "game/rtti.h"

#include <cstdint>
#include <cstring>

namespace sm64nv {

static const uintptr_t kRdataLo = 0x00FDF000, kRdataHi = 0x0118204C;
static const uintptr_t kDataLo = 0x01183000, kDataHi = 0x01271A9C;

// compared this way round so a pointer near the top of memory cannot wrap past the end
static bool in_rdata(uintptr_t p, uintptr_t n) { return p >= kRdataLo && p <= kRdataHi - n; }
static bool in_data(uintptr_t p, uintptr_t n) { return p >= kDataLo && p <= kDataHi - n; }
static uintptr_t word(uintptr_t p) { return *reinterpret_cast<const uintptr_t *>(p); }

static uintptr_t locator_of(const void *obj) {
    if (!obj) return 0;
    uintptr_t vt = word(reinterpret_cast<uintptr_t>(obj));
    if (!in_rdata(vt - 4, 4)) return 0;
    uintptr_t col = word(vt - 4);
    return in_rdata(col, 20) ? col : 0;
}

static const char *type_name(uintptr_t td) { return in_data(td, 12) ? reinterpret_cast<const char *>(td + 8) : ""; }

const char *rtti_name(const void *obj) {
    uintptr_t col = locator_of(obj);
    return col ? type_name(word(col + 12)) : "";
}

bool rtti_is(const void *obj, const char *name) {
    uintptr_t col = locator_of(obj);
    if (!col) return false;
    uintptr_t chd = word(col + 16);
    if (!in_rdata(chd, 16)) return false;
    uintptr_t count = word(chd + 8), bases = word(chd + 12);
    if (count > 64 || !in_rdata(bases, count * 4)) return false;
    for (uintptr_t i = 0; i < count; i++) {
        uintptr_t bcd = word(bases + i * 4);
        if (in_rdata(bcd, 4) && !strcmp(type_name(word(bcd)), name)) return true;
    }
    return false;
}

}
