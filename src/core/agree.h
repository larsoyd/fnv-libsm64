#pragma once

namespace sm64nv {

// readings that should match, where a few can be off for a reason of their own
struct Agreement {
    // one reading in this many may be off
    static constexpr int kShare = 4;
    float limit;
    int samples = 0, off = 0;
    float worst = 0;

    // true when this reading is the furthest off so far
    bool add(float err);
    // reading it wrong puts most of them off, something animated only itself
    bool holds() const;
};

}
