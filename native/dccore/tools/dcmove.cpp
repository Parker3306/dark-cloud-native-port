// Generates random MoveCheck test cases on a generated floor and the C++ port's answers, as text, for tools/dc/validate_move.py (which runs the same
// cases through the game's own MoveCheck in PCSX2 and compares).
// usage: dcmove <iso> <dungeon stem e.g. d01main_a> <seed> <rooms> <map_no> <cases> <rng seed> [spread]
//   case line:  C n x y z vx vy vz mask      then n lines  P <20 hex words>      then  R ox oy oz vx vy vz blocked grounded foot_found hx hy hz
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include "dccore/collision.h"
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/floor.h"
#include "dccore/pack.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: dcmove <iso> <stem> <seed> <rooms> <map_no> <cases> <rng seed> [speed]\n");
        return 2;
    }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const std::string stem = argv[2];
        Bytes cfg = disc.read("dun/" + stem + ".cfg");
        Pack pack(disc.read("dun/mpd_pack/" + stem + ".mpd"));
        Dungeon dun = load_dungeon(std::string(cfg.begin(), cfg.end()), pack);
        FloorLayout layout = build_random_map(uint32_t(std::strtoul(argv[3], nullptr, 0)), std::atoi(argv[4]), std::atoi(argv[5]));
        const int cases = std::atoi(argv[6]);
        std::mt19937 rng(uint32_t(std::strtoul(argv[7], nullptr, 0)));
        const float speed = argc > 8 ? float(std::atof(argv[8])) : 4.0f;
        auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };

        // candidate cells: any cell with collision
        std::vector<std::pair<int, int>> cells;
        for (int y = 0; y < kGrid; ++y)
            for (int x = 0; x < kGrid; ++x)
                if (layout.at(x, y).part >= 0 && layout.at(x, y).part < 0x2c) cells.emplace_back(x, y);
        int made = 0;
        for (int tries = 0; made < cases && tries < cases * 20; ++tries) {
            auto [cx, cy] = cells[size_t(rng() % cells.size())];
            const float x = (float(cx) + uni(-0.5f, 0.5f)) * kCellSize, z = (float(cy) + uni(-0.5f, 0.5f)) * kCellSize;
            std::vector<CCPoly> polys = floor_polys_near(dun, layout, x, z);
            if (polys.empty() || polys.size() > 400) continue;
            // start on the floor if there is one below; half of the time walk to the edge of the floor (walls, cliffs) and stand just inside it
            CCPoly foot;
            float fh[3];
            float px = x, pz = z, y = 0;
            float probe[3] = {px, 80.0f, pz};
            if (get_foot_poly(200.0f, probe, foot, fh, polys.data(), int(polys.size()), 1)) {
                y = fh[1];
                if (rng() % 2 == 0) {
                    const float da = uni(0.0f, 6.2831853f), dxs = std::cos(da), dzs = std::sin(da);
                    float lx = px, lz = pz, ly = y;
                    for (int step = 0; step < 160; ++step) {
                        const float tx = lx + dxs, tz = lz + dzs;
                        const float pr[3] = {tx, ly + 20.0f, tz};
                        CCPoly f2;
                        float h2[3];
                        if (!get_foot_poly(60.0f, pr, f2, h2, polys.data(), int(polys.size()), 1)) break;
                        lx = tx; lz = tz; ly = h2[1];
                    }
                    const float back = uni(0.0f, 7.0f);
                    px = lx - dxs * back; pz = lz - dzs * back; y = ly;
                }
                y += uni(0.0f, 2.0f);
            } else {
                y = uni(-5.0f, 30.0f);
            }
            const float ang = uni(0.0f, 6.2831853f), sp = uni(0.0f, speed * ((rng() % 3 == 0) ? 3.0f : 1.0f));
            float vel[3] = {std::cos(ang) * sp, uni(-1.0f, 0.2f), std::sin(ang) * sp};
            const int mask = (rng() % 8 == 0) ? 8 : 1;
            std::printf("C %zu %.9g %.9g %.9g %.9g %.9g %.9g %d\n", polys.size(), px, y, pz, vel[0], vel[1], vel[2], mask);
            for (const CCPoly& q : polys) {
                std::printf("P");
                const uint32_t* w = reinterpret_cast<const uint32_t*>(&q);
                for (int k = 0; k < 20; ++k) std::printf(" %08x", w[k]);
                std::printf("\n");
            }
            const float pos[3] = {px, y, pz};
            float out[3], v2[3] = {vel[0], vel[1], vel[2]};
            MoveInfo info;
            std::vector<CCPoly> work = polys;
            const bool blocked = move_check(pos, v2, out, info, work.data(), int(work.size()), mask);
            std::printf("R %.9g %.9g %.9g %.9g %.9g %.9g %d %d %d %.9g %.9g %.9g\n", out[0], out[1], out[2], v2[0], v2[1], v2[2], blocked ? 1 : 0, info.grounded, info.foot_found,
                        info.foot_hit[0], info.foot_hit[1], info.foot_hit[2]);
            // segment tests on the same polygons: C++ answers for check_hit (first hit and nearest)
            for (int k = 0; k < 6; ++k) {
                const float a[3] = {px + uni(-12, 12), y + uni(-2, 40), pz + uni(-12, 12)};
                const float b[3] = {a[0] + uni(-10, 10), a[1] + uni(-45, 5), a[2] + uni(-10, 10)};
                float h0[3] = {0, 0, 0}, h1[3] = {0, 0, 0};
                const int i0 = check_hit(polys.data(), int(polys.size()), a, b, h0, false, mask);
                const int i1 = check_hit(polys.data(), int(polys.size()), a, b, h1, true, mask);
                std::printf("S %.9g %.9g %.9g %.9g %.9g %.9g %d %d %.9g %.9g %.9g %d %.9g %.9g %.9g\n", a[0], a[1], a[2], b[0], b[1], b[2], mask, i0, h0[0], h0[1], h0[2], i1, h1[0], h1[1], h1[2]);
            }
            ++made;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
