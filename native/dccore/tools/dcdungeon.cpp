// Prints the floor layout for a seed: dcdungeon <seed> <rooms> <map_no> [--old] [--rn]
// Output: line 1 "rooms n", then n lines "x y w h", then 400 lines "part rot flags" (row by row), then "rand_state".
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/tables.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: dcdungeon <seed> <rooms> <map_no> [--old] [--rn]\n");
        return 2;
    }
    if (argc >= 5 && !std::strcmp(argv[4], "--derive")) {
        std::puts(std::to_string(dccore::derive_floor_seed(uint32_t(std::strtoul(argv[1], nullptr, 0)))).c_str());
        return 0;
    }
    bool fresh = true;
    dccore::GenState state;
    std::unique_ptr<dccore::GameTables> tables;
    for (int i = 4; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--old")) fresh = false;
        if (!std::strcmp(argv[i], "--rn")) dccore::set_dungeon_round_to_zero(false);
        if (!std::strcmp(argv[i], "--iso") && i + 1 < argc) {   // load the game tables from the disc: item positions use the real SearchiDoPutArea
            dccore::Disc disc{std::filesystem::path(argv[++i])};
            tables = std::make_unique<dccore::GameTables>(dccore::GameTables::load(dccore::Elf(disc.read_root("SCUS_971.11"))));
            state.tables = tables.get();
        }
        if (!std::strcmp(argv[i], "--scan") && i + 1 < argc) {  // comma separated initial scan array (256 ints)
            char* p = argv[++i];
            for (size_t k = 0; k < state.scan.size() && *p; ++k) {
                state.scan[k] = int32_t(std::strtol(p, &p, 10));
                if (*p == ',') ++p;
            }
        }
    }
    dccore::FloorLayout f = dccore::build_random_map(uint32_t(std::strtoul(argv[1], nullptr, 0)), std::atoi(argv[2]), std::atoi(argv[3]), fresh, &state);
    std::printf("rooms %zu\n", f.rooms.size());
    for (const auto& r : f.rooms) std::printf("%d %d %d %d\n", r.x, r.y, r.w, r.h);
    for (const auto& c : f.cell) std::printf("%d %d %u\n", c.part, c.rot, c.flags);
    std::printf("rand_state %u\n", f.rand_state);
    std::printf("scan");
    for (int32_t v : state.scan) std::printf(" %d", v);
    std::puts("");
    std::printf("boxes %zu", f.boxes.size());
    for (const auto& b : f.boxes) std::printf(" %.4f %.4f %.4f", b[0], b[1], b[2]);
    std::puts("");
    return 0;
}
