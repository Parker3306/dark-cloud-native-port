#include "dccore/placement.h"
#include <algorithm>
#include <cmath>
#include "dccore/gamemath.h"

namespace dccore {

PutPosition search_put_area(const FloorLayout& layout, const GameTables& tables, int map_no, int rx, int ry, int rw, int rh, GameRand& rng) {
    using namespace gm;
    const std::vector<FreeAreaEntry>& entries = tables.free_areas[size_t(map_no)];
    struct Quad { float c[4][3]; };
    std::vector<Quad> quads;
    constexpr float kPi = 3.14159274101257324f;   // DAT_002a18a0
    int n = 0;
    for (int gy = ry; gy < ry + rh; ++gy) {
        for (int gx = rx; gx < rx + rw; ++gx) {
            const int part = gx >= 0 && gx < kGrid && gy >= 0 && gy < kGrid ? layout.at(gx, gy).part : -1;
            const int cell_rot = gx >= 0 && gx < kGrid && gy >= 0 && gy < kGrid ? layout.at(gx, gy).rot : 0;
            for (size_t e = 0; e < entries.size() && n <= 0xc3; ++e) {
                if (part != entries[e].part) continue;
                for (int q = 0; q < entries[e].count; ++q) {
                    int rot = entries[e].rot + cell_rot;
                    if (3 < rot) rot -= 4;
                    const float theta = fdiv(fmul(fmul(kPi, itof(4 - rot)), 90.0f), 180.0f);
                    const float* f = entries[e].box[q];
                    const float x1 = fmul(f[0], 10.0f), y1 = fmul(f[1], 10.0f), z1 = fmul(f[2], 10.0f), x2 = fmul(f[3], 10.0f), y2 = fmul(f[4], 10.0f), z2 = fmul(f[5], 10.0f);
                    const float A[4] = {x1, x2, x1, x2}, B[4] = {y1, y1, y1, y2}, C[4] = {z1, z1, z2, z2};
                    Quad out{};
                    for (int i = 0; i < 4; ++i) {
                        const float s = std::sin(theta), c = std::cos(theta);
                        const float t = C[i], a = A[i];
                        const float xr = fsub(fmul(-t, s), fmul(a, c));
                        out.c[i][0] = fadd(fmul(xr, -1.0f), itof(gx * 160));
                        out.c[i][2] = fadd(fadd(fmul(-a, s), fmul(t, c)), itof(gy * 160));
                        out.c[i][1] = B[i];
                    }
                    quads.push_back(out);
                    ++n;
                    if (n > 0xc3) break;
                }
            }
        }
    }
    PutPosition r;
    r.areas = n;
    const int pick = scaled(n, rng.next());
    const Quad& qd = quads.empty() ? Quad{} : quads[size_t(std::min<int>(std::max(pick, 0), int(quads.size()) - 1))];
    float minx = qd.c[0][0], maxx = minx, minz = qd.c[0][2], maxz = minz;
    for (int i = 1; i < 4; ++i) {
        minx = std::min(minx, qd.c[i][0]);
        maxx = std::max(maxx, qd.c[i][0]);
        minz = std::min(minz, qd.c[i][2]);
        maxz = std::max(maxz, qd.c[i][2]);
    }
    const float w = fsub(maxx, minx), d = fsub(maxz, minz);
    r.x = fadd(minx, fdiv(fmul(w, itof(rng.next())), kRandRange));
    r.y = qd.c[0][1];
    r.z = fadd(minz, fdiv(fmul(d, itof(rng.next())), kRandRange));
    r.w = 1.0f;
    return r;
}

}  // namespace dccore
