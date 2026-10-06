#pragma once
#include <cstdint>
#include <string>

namespace sm64nv {

struct SoundStats {
    uint32_t written, done, queued, errors, resets;
    int16_t peak;
};

// mario's own 32 kHz stream beside the game's sound, fed from the main thread
bool sound_open(std::string &why);
bool sound_ready();
// tops the queue up with sm64 audio ticks, the same thread must run the mario ticks
void sound_pump();
// counts so far and the loudest sample since the last call
SoundStats sound_take_stats();
// holds the device so it stops handing buffers back, for testing the stall recovery
void sound_pause();

}
