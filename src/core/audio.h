#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sm64nv {

inline constexpr int kAudioRate = 32000;
// one sm64 audio tick writes two blocks of up to 544 stereo frames
inline constexpr size_t kAudioTickFrames = 2 * 544;

// appends one tick of sm64 audio and returns the stereo frames added
// a queue shorter than target gets the longer block
size_t audio_tick(uint32_t queued, uint32_t target, std::vector<int16_t> &out);
int16_t peak(const int16_t *s, size_t n);

}
