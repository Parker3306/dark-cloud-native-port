// dcfloormap <iso> [dungeon 0-6]: which map file each floor of the dungeon uses, according to the dungeon's event script (function 100).
#include <cstdio>
#include <cstdlib>
#include "dccore/disc.h"
#include "dccore/dungeon_script.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dcfloormap <iso> [dungeon 0-6]\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const int first = argc > 2 ? std::atoi(argv[2]) : 0, last = argc > 2 ? first : 6;
        for (int d = first; d <= last; ++d) {
            char path[64];
            std::snprintf(path, sizeof path, "dun/script/d%02d/event.stb", d + 1);
            if (!disc.contains(path)) continue;
            const Bytes stb = disc.read(path);
            std::printf("dungeon %d:", d);
            std::string prev;
            for (int f = 0; f < 40; ++f) {
                const FloorMapChoice c = choose_floor_map(stb, f);
                std::printf(" %d=%s/%d", f, c.name.c_str(), c.kind);
                if (c.name == "d01boss" || c.name.find("boss") != std::string::npos) break;
            }
            std::printf("\n");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
