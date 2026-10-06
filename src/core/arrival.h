#pragma once
#include <cstdint>

namespace sm64nv {

// when to hand mario the player after the game puts the player somewhere new
class ArrivalWatch {
public:
    explicit ArrivalWatch(int settle) : settle_(settle) {}
    // a save load or a jump by the game counts as arriving even in the same place
    void again();
    // place is 0 while none is loaded, true on the frame mario should take the player
    bool frame(uintptr_t place, bool blocked, bool taken);

private:
    uintptr_t last_ = 0, seen_ = 0;
    int settle_, settled_ = 0;
    bool pending_ = false;
};

}
