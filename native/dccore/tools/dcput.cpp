// dcput <iso> <seed> <rooms> <map_no> <rx> <ry> <rw> <rh> <rand state>: the C++ SearchiDoPutArea result for a floor and a rand() state, for tools/dc/validate_put.py
#include <cstdio>
#include <cstdlib>
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/placement.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 10) return 2;
    try {
        Disc disc{std::filesystem::path(argv[1])};
        Elf elf(disc.read_root("SCUS_971.11"));
        GameTables tables = GameTables::load(elf);
        const int map = std::atoi(argv[4]);
        FloorLayout layout = build_random_map(uint32_t(std::strtoul(argv[2], nullptr, 0)), std::atoi(argv[3]), map);
        GameRand rng;
        rng.seed(uint32_t(std::strtoul(argv[9], nullptr, 0)));
        PutPosition p = search_put_area(layout, tables, map, std::atoi(argv[5]), std::atoi(argv[6]), std::atoi(argv[7]), std::atoi(argv[8]), rng);
        std::printf("%d %.6f %.6f %.6f %u\n", p.areas, p.x, p.y, p.z, rng.state);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
