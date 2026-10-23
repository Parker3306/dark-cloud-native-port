// dcpreset <iso> <map> <floor> <table> <small> <rand_state> [count]: EventTables::preset_item (the game's PresetSmallItemNo_Get) `count` times in a row from that generator state.
// Output per call: "item <id> rand <state after>".
#include <cstdio>
#include <cstdlib>
#include "dccore/disc.h"
#include "dccore/events.h"
#include "dccore/dungeon.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: dcpreset <iso> <map> <floor> <table> <small> <rand_state> [count]\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        EventTables et = EventTables::load(Elf(disc.read_root("SCUS_971.11")));
        GameRand rng;
        rng.seed(uint32_t(std::strtoul(argv[6], nullptr, 0)));
        const int count = argc > 7 ? std::atoi(argv[7]) : 1;
        for (int i = 0; i < count; ++i) {
            const int item = et.preset_item(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::atoi(argv[5]) != 0, rng);
            std::printf("item %d rand %u\n", item, rng.state);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
