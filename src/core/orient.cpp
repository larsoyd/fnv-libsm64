#include "orient.h"

#include <cmath>
#include <unordered_map>

namespace sm64nv {

namespace {

struct EdgeUse {
    uint32_t tri;
    int dir;
};

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(Vec3 a, Vec3 b) { return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z; }
double len(Vec3 a) { return std::sqrt(dot(a, a)); }

const double kPi = 3.14159265358979;

uint64_t edge_key(uint32_t u, uint32_t v) { return u < v ? (uint64_t)u << 32 | v : (uint64_t)v << 32 | u; }

// signed solid angle, positive when the point sits behind the front face
double solid_angle(Vec3 a, Vec3 b, Vec3 c) {
    double la = len(a), lb = len(b), lc = len(c);
    double num = dot(a, cross(b, c));
    double den = la * lb * lc + dot(a, b) * lc + dot(a, c) * lb + dot(b, c) * la;
    return 2 * std::atan2(num, den);
}

}

void orient_mesh(Mesh &m, Vec3 p, OrientStats &st) {
    size_t n = m.tris.size();
    std::unordered_map<uint64_t, std::vector<EdgeUse>> edges;
    for (uint32_t t = 0; t < n; t++)
        for (int k = 0; k < 3; k++) {
            uint32_t u = m.tris[t][k], v = m.tris[t][(k + 1) % 3];
            edges[edge_key(u, v)].push_back({t, u < v ? 1 : -1});
        }
    auto dir_of = [&](uint32_t t, uint64_t key) {
        for (const EdgeUse &e : edges[key])
            if (e.tri == t) return e.dir;
        return 0;
    };
    std::vector<int> flip(n, -1), comp(n, -1);
    std::vector<uint32_t> queue;
    uint32_t comps = 0;
    for (uint32_t seed = 0; seed < n; seed++) {
        if (flip[seed] >= 0) continue;
        flip[seed] = 0, comp[seed] = comps;
        queue.assign(1, seed);
        bool closed = true;
        for (size_t qi = 0; qi < queue.size(); qi++) {
            uint32_t t = queue[qi];
            for (int k = 0; k < 3; k++) {
                uint64_t key = edge_key(m.tris[t][k], m.tris[t][(k + 1) % 3]);
                const std::vector<EdgeUse> &uses = edges[key];
                if (uses.size() != 2) {
                    closed = false;
                    continue;
                }
                const EdgeUse &other = uses[0].tri == t ? uses[1] : uses[0];
                int eff = dir_of(t, key) * (flip[t] ? -1 : 1);
                int want = other.dir == eff ? 1 : 0;
                if (flip[other.tri] < 0) {
                    flip[other.tri] = want, comp[other.tri] = comps;
                    queue.push_back(other.tri);
                } else if (flip[other.tri] != want) st.conflicts++;
            }
        }
        Vec3 c{0, 0, 0};
        for (uint32_t t : queue)
            for (uint32_t v : m.tris[t]) c = {c.x + m.verts[v].x, c.y + m.verts[v].y, c.z + m.verts[v].z};
        float inv = 1.0f / (3 * queue.size());
        c = {c.x * inv, c.y * inv, c.z * inv};
        double w = 0, vol = 0;
        for (uint32_t t : queue) {
            Vec3 a = m.verts[m.tris[t][0]], b = m.verts[m.tris[t][1]], d = m.verts[m.tris[t][2]];
            if (flip[t]) std::swap(b, d);
            w += solid_angle(sub(a, p), sub(b, p), sub(d, p));
            vol += dot(sub(a, c), cross(sub(b, c), sub(d, c)));
        }
        w /= 4 * kPi;
        bool inside = closed && std::fabs(w) > 0.5;
        bool turn = closed && !inside ? vol < 0 : w > 0;
        st.inside += inside;
        if (turn)
            for (uint32_t t : queue) flip[t] ^= 1;
        comps++;
    }
    for (uint32_t t = 0; t < n; t++)
        if (flip[t]) std::swap(m.tris[t][1], m.tris[t][2]), st.flipped++;
    st.components += comps;
}

}
