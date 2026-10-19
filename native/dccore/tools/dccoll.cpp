// Explore a floor's collision: dccoll <iso> <stem> <seed> <rooms> <map_no> <x> <z>
// Prints every polygon crossed by the vertical line through (x, z) among the polygons the game would gather there.
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>
#include "dccore/collision.h"
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/floor.h"
#include "dccore/pack.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 8) return 2;
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const std::string stem = argv[2];
        Bytes cfg = disc.read("dun/" + stem + ".cfg");
        Pack pack(disc.read("dun/mpd_pack/" + stem + ".mpd"));
        Dungeon dun = load_dungeon(std::string(cfg.begin(), cfg.end()), pack);
        GenState gs;   // DC_ZONE / DC_PARTY / DC_FLOOR: the forced character, the party size and the floor of the floor generator (character rooms)
        if (const char* e = std::getenv("DC_ZONE")) gs.chara_state = std::atoi(e);
        if (const char* e = std::getenv("DC_PARTY")) gs.party_size = std::atoi(e);
        if (const char* e = std::getenv("DC_FLOOR")) gs.floor_no = std::atoi(e);
        FloorLayout layout = build_random_map(uint32_t(std::strtoul(argv[3], nullptr, 0)), std::atoi(argv[4]), std::atoi(argv[5]), true, &gs);
        const float x = float(std::atof(argv[6])), z = float(std::atof(argv[7]));
        if (argc > 8 && std::string(argv[8]) == "--fixed-frames") {   // a boss / story room (fixed map): every frame of its collision pieces and the objects of its models
            for (const auto& kv : dun.collisions) {
                std::printf("collision %s: %zu polygons\n", kv.first.c_str(), kv.second.polys.size());
                for (const auto& fr : kv.second.frames) std::printf("    frame %s %g %g %g\n", fr.first.c_str(), double(fr.second[0]), double(fr.second[1]), double(fr.second[2]));
            }
            for (const auto& kv : dun.marker_frames) {
                std::printf("model %s\n", kv.first.c_str());
                for (const auto& fr : kv.second) std::printf("    object %s %g %g %g\n", fr.first.c_str(), double(fr.second[0]), double(fr.second[1]), double(fr.second[2]));
            }
            return 0;
        }
        if (argc > 8 && std::string(argv[8]) == "--dump") {   // every polygon of the cell containing (x, z) as hex words (cell index = trunc(x / 160))
            auto cell_polys = floor_polys_near(dun, layout, x, z, 0);
            for (const CCPoly& q : cell_polys) {
                std::printf("P");
                const uint32_t* w = reinterpret_cast<const uint32_t*>(&q);
                for (int k = 0; k < 20; ++k) std::printf(" %08x", w[k]);
                std::putchar(10);
            }
            return 0;
        }
        if (argc > 8 && std::string(argv[8]) == "--find") {   // every placed part holding a frame / model object called argv[9] (the part number a script's _GET_OBJHDL names is the cfg part index)
            const std::string want = argc > 9 ? argv[9] : "";
            for (int cy = 0; cy < kGrid; ++cy)
                for (int cx = 0; cx < kGrid; ++cx) {
                    const MapCell& cell = layout.at(cx, cy);
                    if (cell.part < 0 || size_t(cell.part) >= dun.cfg.parts.size()) continue;
                    const PartDef& pd = dun.cfg.parts[size_t(cell.part)];
                    for (const PartRef& cr : pd.collisions) {
                        auto it = dun.collisions.find(cr.file);
                        if (it != dun.collisions.end() && std::any_of(it->second.frames.begin(), it->second.frames.end(), [&](const auto& f) { return f.first == want; })) std::printf("cell %d,%d part %d: collision frame in %s\n", cx, cy, cell.part, cr.file.c_str());
                    }
                    for (const PartModelRef& mr : pd.models) {
                        auto fm = dun.marker_frames.find(mr.file);
                        if (fm == dun.marker_frames.end()) continue;
                        for (const auto& o : fm->second) {
                            const size_t us = o.first.find("__");
                            if (o.first == want || (us != std::string::npos && o.first.compare(0, us, want) == 0)) std::printf("cell %d,%d part %d: object %s in %s at %g %g %g\n", cx, cy, cell.part, o.first.c_str(), mr.file.c_str(), double(o.second[0]), double(o.second[1]), double(o.second[2]));
                        }
                    }
                }
            return 0;
        }
        if (argc > 8 && std::string(argv[8]) == "--frames") {   // the special cells (in-stairs 0x80, out-stairs 0x100, back-floor entrance 0x200) with the frames of their collision pieces
            for (int cy = 0; cy < kGrid; ++cy)
                for (int cx = 0; cx < kGrid; ++cx) {
                    const MapCell& cell = layout.at(cx, cy);
                    if (!(cell.flags & 0x380) && !(cell.part >= 55 && cell.part <= 69)) continue;
                    std::printf("cell %d,%d part %d rot %d flags %x\n", cx, cy, cell.part, cell.rot, cell.flags);
                    if (cell.part < 0 || size_t(cell.part) >= dun.cfg.parts.size()) continue;
                    for (const PartRef& cr : dun.cfg.parts[size_t(cell.part)].collisions) {
                        auto it = dun.collisions.find(cr.file);
                        if (it == dun.collisions.end()) continue;
                        std::printf("  file %s rot %d\n", cr.file.c_str(), cr.rot);
                        for (const auto& fr : it->second.frames) std::printf("    frame %s %g %g %g\n", fr.first.c_str(), double(fr.second[0]), double(fr.second[1]), double(fr.second[2]));
                    }
                    for (const PartModelRef& mr : dun.cfg.parts[size_t(cell.part)].models) {   // the objects of the part's models (_GET_OBJHDL finds them too)
                        auto fm = dun.marker_frames.find(mr.file);
                        if (fm == dun.marker_frames.end()) continue;
                        std::printf("  model %s rot %d\n", mr.file.c_str(), mr.rot);
                        for (const auto& fr : fm->second) std::printf("    object %s %g %g %g\n", fr.first.c_str(), double(fr.second[0]), double(fr.second[1]), double(fr.second[2]));
                    }
                }
            return 0;
        }
        if (argc > 8 && std::string(argv[8]) == "--atr") {   // the collision shape that stands in for a monster (dun/etc/atrcol.mds)
            CollisionPiece piece = parse_collision_mds(disc.read("dun/etc/atrcol.mds"));
            std::printf("%zu polygons\n", piece.polys.size());
            for (const CCPoly& q : piece.polys)
                std::printf("v0 %g %g %g  v1 %g %g %g  v2 %g %g %g  n %g %g %g  attr %08x %08x %08x %08x\n", q.v[0][0], q.v[0][1], q.v[0][2], q.v[1][0], q.v[1][1], q.v[1][2], q.v[2][0], q.v[2][1], q.v[2][2], q.n[0], q.n[1], q.n[2], q.attr[0], q.attr[1], q.attr[2], q.attr[3]);
            return 0;
        }
        if (argc > 8 && std::string(argv[8]) == "--dump3") {   // the 3 x 3 cells around (x, z): what a monster gathers
            for (const CCPoly& q : floor_polys_near(dun, layout, x, z, 1)) {
                std::printf("P");
                const uint32_t* w = reinterpret_cast<const uint32_t*>(&q);
                for (int k = 0; k < 20; ++k) std::printf(" %08x", w[k]);
                std::putchar(10);
            }
            return 0;
        }
        if (argc > 8 && std::string(argv[8]) == "--dump-cam") {   // the camera pieces of the cell containing (x, z)
            const int cx = int(x / kCellSize), cy = int(z / kCellSize);
            const MapCell& cell = layout.at(cx, cy);
            if (cell.part >= 0 && size_t(cell.part) < dun.cfg.parts.size()) {
                const float pos[3] = {float(cx) * kCellSize, 0.0f, float(cy) * kCellSize};
                std::vector<CCPoly> out;
                for (const PartRef& c : dun.cfg.parts[size_t(cell.part)].cameras) {
                    auto it = dun.cam_meshes.find(c.file);
                    if (it != dun.cam_meshes.end()) place_polys(it->second, model_turn_index(cell.rot, c.rot), pos, out);
                }
                for (const CCPoly& q : out) {
                    std::printf("P");
                    const uint32_t* w = reinterpret_cast<const uint32_t*>(&q);
                    for (int k = 0; k < 20; ++k) std::printf(" %08x", w[k]);
                    std::putchar(10);
                }
            }
            return 0;
        }
        if (argc > 8 && std::string(argv[8]) == "--cam") {   // vertical hits of the camera collision at (x, z)
            auto cp = floor_camera_polys(dun, layout, x, z);
            const float a[3] = {x, 100.0f, z}, b[3] = {x, -100.0f, z};
            int idx[64];
            float hits[64][4];
            const int n = check_hits(cp.data(), int(cp.size()), a, b, 64, idx, hits, 1, 0);
            std::printf("%zu camera polygons; %d hits\n", cp.size(), n);
            for (int i = 0; i < n; ++i) {
                const CCPoly& q = cp[size_t(idx[i])];
                std::printf("hit y=%.3f normal(%.2f %.2f %.2f)\n", hits[i][1], q.n[0], q.n[1], q.n[2]);
            }
            return 0;
        }
        auto polys = floor_polys_near(dun, layout, x, z);
        if (argc > 8 && std::string(argv[8]) == "--map") {   // ASCII map of the floor around (x, z): F = floor found within 100 below y=60, '#' = steep, '.' = nothing
            for (float zz = z - 120; zz <= z + 120; zz += 8) {
                for (float xx = x - 200; xx <= x + 200; xx += 5) {
                    const float a[3] = {xx, 60.0f, zz}, b[3] = {xx, -100.0f, zz};
                    int idx[16];
                    float hits[16][4];
                    const int n = check_hits(polys.data(), int(polys.size()), a, b, 16, idx, hits, 1, 0);
                    char c = '.';
                    if (n > 0) c = polys[size_t(idx[0])].n[1] / std::sqrt(polys[size_t(idx[0])].n[0] * polys[size_t(idx[0])].n[0] + polys[size_t(idx[0])].n[1] * polys[size_t(idx[0])].n[1] + polys[size_t(idx[0])].n[2] * polys[size_t(idx[0])].n[2]) > 0.5f ? 'F' : '#';
                    std::putchar(c);
                }
                std::putchar(10);
            }
            return 0;
        }
        std::printf("%zu polygons near; cell (%d,%d) part %d rot %d\n", polys.size(), int(x / kCellSize), int(z / kCellSize), layout.at(int(x / kCellSize), int(z / kCellSize)).part,
                    layout.at(int(x / kCellSize), int(z / kCellSize)).rot);
        const float a[3] = {x, 500.0f, z}, b[3] = {x, -500.0f, z};
        int idx[64];
        float hits[64][4];
        const int n = check_hits(polys.data(), int(polys.size()), a, b, 64, idx, hits, 1, 0);
        for (int i = 0; i < n; ++i) {
            const CCPoly& q = polys[size_t(idx[i])];
            float nn[3] = {q.n[0], q.n[1], q.n[2]};
            const float l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            std::printf("hit y=%.3f poly %d normal(%.2f %.2f %.2f) attr %08x %08x %08x %08x type %d\n", hits[i][1], idx[i], nn[0] / l, nn[1] / l, nn[2] / l, q.attr[0], q.attr[1], q.attr[2], q.attr[3], q.type());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
