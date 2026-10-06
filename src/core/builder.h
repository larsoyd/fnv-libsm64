#pragma once
#include "surfaces.h"
#include "window.h"

#include <future>
#include <optional>
#include <vector>

namespace sm64nv {

struct Gather {
    Frame frame;
    std::vector<Tri> tris;
    float radius, reach;
};

struct Built {
    SurfaceWindow window;
    SurfaceStats stats;
    // the index in tris of each surface, its owner and whether it is a face of a closed shape
    std::vector<uint32_t> kept, owners;
    std::vector<bool> fixed;
    // spent on the surfaces and on the window
    float build_ms = 0, window_ms = 0;
};

Built build_world(Gather g);

// builds a gather's surfaces and window on its own thread, one at a time
class Builder {
public:
    bool start(Gather g);
    bool busy() const;
    // the result once, when the thread has finished
    std::optional<Built> take();

private:
    std::future<Built> pending_;
};

}
