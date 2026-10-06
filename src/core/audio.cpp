#include "audio.h"
#include "libsm64.h"

#include <algorithm>
#include <cstdlib>

namespace sm64nv {

size_t audio_tick(uint32_t queued, uint32_t target, std::vector<int16_t> &out) {
    size_t at = out.size();
    out.resize(at + 2 * kAudioTickFrames);
    uint32_t n = sm64_audio_tick(queued, target, out.data() + at);
    out.resize(at + 4 * n);
    return 2 * n;
}

bool StreamWatch::stalled(uint64_t now_ms, uint32_t done, bool full) {
    if (!started_ || done != done_ || !full) {
        since_ = now_ms, done_ = done, started_ = true;
        return false;
    }
    if (now_ms - since_ < kStallMs) return false;
    since_ = now_ms;
    return true;
}

int16_t peak(const int16_t *s, size_t n) {
    int m = 0;
    for (size_t i = 0; i < n; i++) m = std::max(m, std::abs((int)s[i]));
    return (int16_t)std::min(m, 32767);
}

}
