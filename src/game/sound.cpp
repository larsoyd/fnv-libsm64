#include "game/sound.h"
#include "core/audio.h"

#include <algorithm>
#include <vector>
#include <windows.h>

namespace sm64nv {

namespace {

const int kBuffers = 6;
const uint32_t kTargetFrames = 2 * kAudioTickFrames;

HWAVEOUT g_out;
WAVEHDR g_hdr[kBuffers];
std::vector<int16_t> g_pcm[kBuffers];
uint32_t g_written, g_done;
int16_t g_peak;

uint32_t queued_frames() {
    uint32_t n = 0;
    for (const WAVEHDR &h : g_hdr)
        if (!(h.dwFlags & WHDR_DONE)) n += h.dwBufferLength / 4;
    return n;
}

}

bool sound_open(std::string &why) {
    WAVEFORMATEX f{WAVE_FORMAT_PCM, 2, kAudioRate, kAudioRate * 4, 4, 16, 0};
    MMRESULT r = waveOutOpen(&g_out, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        why = "waveOutOpen=" + std::to_string(r);
        return false;
    }
    for (WAVEHDR &h : g_hdr) h.dwFlags = WHDR_DONE;
    return true;
}

bool sound_ready() { return g_out != nullptr; }

void sound_pump() {
    for (int i = 0; i < kBuffers; i++) {
        WAVEHDR &h = g_hdr[i];
        if (!(h.dwFlags & WHDR_DONE)) continue;
        if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_out, &h, sizeof h), g_done++;
        uint32_t queued = queued_frames();
        if (queued >= kTargetFrames) continue;
        g_pcm[i].clear();
        audio_tick(queued, kTargetFrames, g_pcm[i]);
        g_peak = std::max(g_peak, peak(g_pcm[i].data(), g_pcm[i].size()));
        h = {};
        h.lpData = reinterpret_cast<LPSTR>(g_pcm[i].data());
        h.dwBufferLength = (DWORD)(g_pcm[i].size() * sizeof(int16_t));
        waveOutPrepareHeader(g_out, &h, sizeof h);
        waveOutWrite(g_out, &h, sizeof h);
        g_written++;
    }
}

SoundStats sound_take_stats() {
    SoundStats s{g_written, g_done, g_peak};
    g_peak = 0;
    return s;
}

}
