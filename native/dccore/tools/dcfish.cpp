// dcfish <iso> [set] [bait]: loads the fishing tables and simulates a pond with the hook in the middle: prints the fish and how often they bite.
#include <cstdio>
#include <cstdlib>
#include "dccore/disc.h"
#include "dccore/fishing.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dcfish <iso> [set] [bait]\n"); return 2; }
    Disc disc{std::filesystem::path(argv[1])};
    const FishTables t = FishTables::load(Elf(disc.read_root("SCUS_971.11")));
    const int set = argc > 2 ? std::atoi(argv[2]) : 0;
    const int bait = argc > 3 ? std::atoi(argv[3]) : 0;
    for (int k = 0; k < 18; ++k) std::printf("fish %2d: len %.1f..%.1f fp %d..%d attract[bait %d] %.2f\n", k, t.fish[k].min_len, t.fish[k].max_len, t.fish[k].fp_min, t.fish[k].fp_max, bait, t.fish[k].attract[bait]);
    for (int b = 0; b < 13; ++b) std::printf("bait %d: item %d radius %.0f\n", b, t.bait_item[b], t.bait_radius[b]);
    GameRand rng;
    rng.seed(7);
    FishingSim sim(&t, &rng);
    sim.water_y = -13;
    sim.load(set, 0);
    sim.init_area(-50, -50, 50, 50);
    sim.bait = bait;
    int bites = 0, nibbles = 0;
    int last = -1;
    for (int tick = 0; tick < 36000; ++tick) {
        const float hook[3] = {0, -13 - 8.3f, 0};
        sim.step(hook);
        int idx = -1;
        const int st = sim.status(&idx);
        if (st != last) {
            if (st == 8) ++bites;
            if (st == 7) ++nibbles;
            last = st;
        }
    }
    std::printf("set %d bait %d: %d nibbles, %d hooked in 10 minutes\n", set, bait, nibbles, bites);
    for (int i = 0; i < sim.fish_num; ++i) {
        const FishCatch c = sim.catch_of(i);
        std::printf("pond fish %d: kind %d len %.1f -> size %d, %d fp\n", i, c.kind, sim.fish[size_t(i)].len, c.size10, c.points);
    }
}
