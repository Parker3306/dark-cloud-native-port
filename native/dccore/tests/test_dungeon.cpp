// Regression test for the floor generator. The layouts were checked cell by cell against the game's own CDungeonMap::buildRandomMap running in PCSX2
// (tools/dc/validate_gen.py: 230 floors, 2..12 rooms, identical); the hashes below pin the current output of the port. Run with --print to list new ones.
#include <cstdio>
#include <cstring>
#include "dccore/dungeon.h"

namespace {
uint64_t hash_layout(const dccore::FloorLayout& f) {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    for (const auto& r : f.rooms) { mix(uint64_t(r.x)); mix(uint64_t(r.y)); mix(uint64_t(r.w)); mix(uint64_t(r.h)); }
    for (const auto& c : f.cell) { mix(uint64_t(uint32_t(c.part))); mix(uint64_t(uint32_t(c.rot))); mix(c.flags); }
    return h;
}
struct Case { uint32_t seed; int rooms; uint64_t hash; };
const Case kCases[] = {
    {8097, 6, 0x96473eeafcd49e3bull},  {10795, 6, 0x7403ca64356f6f97ull}, {15305, 3, 0xf7f5a00ee39feeb5ull}, {12762, 7, 0x6458883bcccc033full},
    {20464, 8, 0x8035170fd0019909ull}, {1546, 3, 0xa1a1c6c6d1565653ull},  {11161, 5, 0x59d17c820efca26cull}, {11894, 6, 0x455c1c74f797240bull},
};
}  // namespace

int main(int argc, char** argv) {
    const bool print = argc > 1 && !std::strcmp(argv[1], "--print");
    int bad = 0;
    for (const Case& c : kCases) {
        const auto f = dccore::build_random_map(c.seed, c.rooms, 0);
        const uint64_t h = hash_layout(f);
        if (print) std::printf("    {%u, %d, 0x%016llxull},\n", c.seed, c.rooms, static_cast<unsigned long long>(h));
        else if (h != c.hash) { std::printf("floor seed %u rooms %d: hash %016llx, expected %016llx\n", c.seed, c.rooms, static_cast<unsigned long long>(h), static_cast<unsigned long long>(c.hash)); ++bad; }
        // invariants: every room lies inside the 16x16 area, rooms never overlap, the start and both stairs exist
        int stairs_up = 0, stairs_down = 0, starts = 0;
        for (const auto& cell : f.cell) {
            stairs_up += (cell.flags & 0x80) != 0;
            stairs_down += (cell.flags & 0x100) != 0;
            starts += (cell.flags & 0x200) != 0;
        }
        if (f.rooms.size() < 2 || int(f.rooms.size()) > c.rooms || stairs_up != 1 || stairs_down != 1 || starts != 1) {
            std::printf("floor seed %u: rooms %zu up %d down %d start %d\n", c.seed, f.rooms.size(), stairs_up, stairs_down, starts);
            ++bad;
        }
    }
    return bad ? 1 : 0;
}
