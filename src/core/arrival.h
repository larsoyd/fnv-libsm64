#pragma once
#include <cstdint>

namespace sm64nv {

// when to hand mario the player after a cell change or a save load
class ArrivalWatch {
public:
    explicit ArrivalWatch(int settle) : settle_(settle) {}
    // a save load counts as arriving again even in the same cell
    void loaded_game();
    // cell is 0 while none is loaded, true on the frame mario should take the player
    bool frame(uintptr_t cell, bool blocked, bool taken);

private:
    uintptr_t last_ = 0, seen_ = 0;
    int settle_, settled_ = 0;
    bool pending_ = false;
};

}
