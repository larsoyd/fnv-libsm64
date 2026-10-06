#include "builder.h"

#include <chrono>

namespace sm64nv {

Built build_world(Gather g) {
    using clock = std::chrono::steady_clock;
    auto ms_since = [](clock::time_point t) { return std::chrono::duration<float, std::milli>(clock::now() - t).count(); };
    SurfaceStats stats;
    std::vector<uint32_t> kept, owners;
    clock::time_point t0 = clock::now();
    std::vector<SM64Surface> surfaces = build_surfaces(g.frame, g.tris, stats, &kept);
    std::vector<bool> fixed;
    for (uint32_t i : kept) fixed.push_back(g.tris[i].solid), owners.push_back(g.tris[i].owner);
    float build_ms = ms_since(t0);
    clock::time_point t1 = clock::now();
    SurfaceWindow window(std::move(surfaces), g.radius, g.reach, fixed);
    return {std::move(window), stats, std::move(kept), std::move(owners), std::move(fixed), build_ms, ms_since(t1)};
}

bool Builder::start(Gather g) {
    if (pending_.valid()) return false;
    pending_ = std::async(std::launch::async, build_world, std::move(g));
    return true;
}

bool Builder::busy() const { return pending_.valid(); }

std::optional<Built> Builder::take() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return {};
    return pending_.get();
}

}
