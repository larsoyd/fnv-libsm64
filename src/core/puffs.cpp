#include "puffs.h"
#include "geo.h"
#include "mio0.h"

#include <cmath>
#include <cstring>
#include <map>

namespace sm64nv {

namespace {

// blocks of packed pictures in the us rom and where each picture starts once unpacked
struct Picture {
    size_t block, offset;
};
const size_t kEffects = 2102288, kCommon = 1132368;
const Picture kPictures[] = {{kEffects, 128},    {kCommon, 122528}, {kCommon, 124576}, {kCommon, 126624},
                             {kCommon, 128672}, {kCommon, 130720}, {kCommon, 132768}, {kCommon, 134816}};
const int kWhiteCell = 8;

float cell_u(int cell, float texel) { return (cell * kPuffCell + texel + 0.5f) / kPuffAtlasWidth; }
float cell_v(float texel) { return (texel + 0.5f) / kPuffCell; }

// a picture over a rectangle, its top row along the top edge
std::vector<PuffVertex> card(float x0, float y0, float x1, float y1, int cell, float alpha) {
    float u0 = cell_u(cell, 0), u1 = cell_u(cell, kPuffCell - 1), v0 = cell_v(0), v1 = cell_v(kPuffCell - 1);
    PuffVertex a{x0, y0, u0, v1, {1, 1, 1, alpha}}, b{x1, y0, u1, v1, {1, 1, 1, alpha}};
    PuffVertex c{x1, y1, u1, v0, {1, 1, 1, alpha}}, d{x0, y1, u0, v0, {1, 1, 1, alpha}};
    return {a, b, c, a, c, d};
}

// plain yellow triangles from corner pairs, turned counter clockwise where they are not
std::vector<PuffVertex> yellow(std::span<const float> xy) {
    std::vector<PuffVertex> out;
    for (size_t i = 0; i + 1 < xy.size(); i += 2) out.push_back({xy[i], xy[i + 1], cell_u(kWhiteCell, 15.5f), cell_v(15.5f), {1, 1, 0, 1}});
    for (size_t i = 0; i < out.size(); i += 3) {
        PuffVertex &a = out[i], &b = out[i + 1], &c = out[i + 2];
        if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) < 0) std::swap(b, c);
    }
    return out;
}

const float kStar[] = {0,   -8, -32, 80, 32,  80,  -52, 28, -116, 80, -32, 80,  0,  -8, -84, -52, -52, 28, 52,  28, 84, -52, 0,  -8,
                       32,  80, 116, 80, 52,  28,  -32, 80, 0,    160, 32, 80,  0,  -8, -52, 28,  -32, 80, 32,  80, 52, 28,  0,  -8};
const float kShard[] = {-10, 10, 10, 10, 0, -10};

Vec3 unit(Vec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return {v.x / len, v.y / len, v.z / len};
}

}

std::vector<uint8_t> puff_atlas(std::span<const uint8_t> rom) {
    std::vector<uint8_t> atlas(4 * kPuffAtlasWidth * kPuffCell, 0);
    const size_t bytes = 2 * kPuffCell * kPuffCell;
    std::map<size_t, std::vector<uint8_t>> blocks;
    for (const Picture &p : kPictures)
        if (!blocks.count(p.block)) blocks[p.block] = mio0_unpack(rom, p.block, kPictures[p.block == kCommon ? 7 : 0].offset + bytes);
    for (int cell = 0; cell < kWhiteCell; cell++) {
        const std::vector<uint8_t> &block = blocks[kPictures[cell].block];
        if (block.empty()) return {};
        // two bytes a texel, how bright and how solid
        const uint8_t *src = &block[kPictures[cell].offset];
        for (int i = 0; i < kPuffCell * kPuffCell; i++) {
            uint8_t *o = &atlas[4 * (i / kPuffCell * kPuffAtlasWidth + cell * kPuffCell + i % kPuffCell)];
            o[0] = o[1] = o[2] = src[2 * i], o[3] = src[2 * i + 1];
        }
    }
    for (int y = 0; y < kPuffCell; y++) memset(&atlas[4 * (y * kPuffAtlasWidth + kWhiteCell * kPuffCell)], 255, 4 * kPuffCell);
    return atlas;
}

std::vector<PuffVertex> puff_model(Puff model, int frame) {
    switch (model) {
    case Puff::mist:
        return card(-25, -25, 25, 25, 0, 1);
    case Puff::smoke:
        return card(-32, 0, 32, 64, 1 + std::min(frame, 6), 100 / 255.0f);
    case Puff::star:
        return yellow(kStar);
    default:
        return yellow(kShard);
    }
}

void build_puffs(const Frame &f, const std::vector<Particle> &alive, float blend, Vec3 eye, Vec3 anchor, MeshOut &out) {
    clear_mesh(out);
    size_t n = 0;
    for (const Particle &p : alive) {
        std::vector<PuffVertex> model = puff_model(p.model, p.age);
        if (p.size <= 0 || n + model.size() > out.pos.size()) continue;
        Vec3 c = to_game(f, lerp(p.prev, p.pos, blend));
        Vec3 view = unit({eye.x - c.x, eye.y - c.y, eye.z - c.z});
        // across is level, from straight above or below any level way will do
        float level = std::hypot(view.x, view.y);
        Vec3 across = level > 1e-4f ? Vec3{-view.y / level, view.x / level, 0} : Vec3{1, 0, 0};
        Vec3 up{view.y * across.z - view.z * across.y, view.z * across.x - view.x * across.z, view.x * across.y - view.y * across.x};
        for (const PuffVertex &v : model) {
            float x = v.x * p.size / f.scale, y = v.y * p.size / f.scale;
            out.pos[n] = {c.x + across.x * x + up.x * y - anchor.x, c.y + across.y * x + up.y * y - anchor.y, c.z + across.z * x + up.z * y - anchor.z};
            out.normal[n] = view;
            for (int k = 0; k < 4; k++) out.color[n * 4 + k] = v.color[k];
            out.color[n * 4 + 3] *= p.alpha;
            out.uv[n * 2] = v.u, out.uv[n * 2 + 1] = v.v;
            n++;
        }
    }
    out.tris = (uint32_t)(n / 3);
}

}
