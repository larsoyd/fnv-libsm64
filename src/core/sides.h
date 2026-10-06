#pragma once
#include "libsm64.h"

#include <cstdint>
#include <vector>

namespace sm64nv {

// tells which sides of a wall have room for mario among all the surfaces given
class Sides {
public:
    explicit Sides(std::vector<SM64Surface> all);
    // 1 when only the side the wall faces has room for him, -1 when only the other side has
    // 0 when both or neither have, and for floors and ceilings
    int open_side(const SM64Surface &wall) const;

private:
    struct Box {
        float lo[3], hi[3];
    };
    struct Point {
        float x, y, z;
    };
    int column(float x) const;
    int row(float z) const;
    const std::vector<uint32_t> &at(Point p) const;
    float room(Point p) const;
    bool blocked(Point p, Point d) const;
    bool fits(Point p, Point n) const;
    bool open(const SM64Surface &wall, Point n) const;

    std::vector<SM64Surface> all_;
    std::vector<Box> boxes_;
    std::vector<std::vector<uint32_t>> cells_;
    float x0_ = 0, z0_ = 0;
    int nx_ = 0, nz_ = 0;
};

}
