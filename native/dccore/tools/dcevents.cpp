// dcevents <iso> <stem> <seed> <rooms> <map_no> <floor> [--status file] [--rand-state n]: floor events (chests, atra, traps) as the port builds them.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/events.h"
#include "dccore/floor.h"
#include "dccore/pack.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: dcevents <iso> <stem> <seed> <rooms> <map_no> <floor> [--status file] [--rand-state n]\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const std::string stem = argv[2];
        const int map_no = std::atoi(argv[5]), floor = std::atoi(argv[6]);
        Bytes cfg = disc.read("dun/" + stem + ".cfg");
        Pack pack(disc.read("dun/mpd_pack/" + stem + ".mpd"));
        Dungeon dun = load_dungeon(std::string(cfg.begin(), cfg.end()), pack);
        Elf elf(disc.read_root("SCUS_971.11"));
        GameTables tables = GameTables::load(elf);
        EventTables et = EventTables::load(elf);
        UserStatus status = UserStatus::new_game();
        for (int i = 7; i < argc; ++i)
            if (!std::strcmp(argv[i], "--status") && i + 1 < argc) {
                std::ifstream f(argv[i + 1], std::ios::binary);
                f.read(reinterpret_cast<char*>(status.raw.data()), std::streamsize(status.raw.size()));
            }
        GenState gen;
        gen.tables = &tables;
        gen.floor_no = floor;
        gen.party_size = status.get<int8_t>(5);
        gen.chara_state = status.get<int32_t>(0x8b10);
        for (int i = 7; i < argc; ++i)
            if (!std::strcmp(argv[i], "--scan") && i + 1 < argc) {
                const char* q = argv[i + 1];
                char* e = nullptr;
                for (size_t k = 0; k < gen.scan.size() && *q; ++k) {
                    gen.scan[k] = int32_t(std::strtol(q, &e, 10));
                    q = e;
                    if (*q == ',') ++q;
                }
            }
        uint32_t seed = uint32_t(std::strtoul(argv[3], nullptr, 0));
        for (int i = 7; i < argc; ++i)
            if (!std::strcmp(argv[i], "--derive")) seed = derive_floor_seed(seed);   // argv[3] is the generator state the game had before the floor
        FloorLayout layout = build_random_map(seed, std::atoi(argv[4]), map_no, true, &gen);
        GameRand rng;
        rng.seed(layout.rand_state);
        for (int i = 7; i < argc; ++i)
            if (!std::strcmp(argv[i], "--rand-state") && i + 1 < argc) rng.seed(uint32_t(std::strtoul(argv[i + 1], nullptr, 0)));
        std::printf("rand_after_map %u\n", rng.state);
        FloorEvents ev = make_floor_events(layout, map_no);
        build_event_data(ev, layout, tables, et, status, map_no, floor, true, rng);
        std::printf("rand_after_events %u\n", rng.state);
        for (size_t i = 0; i < ev.events.size(); ++i) {
            const FloorEvent& e = ev.events[i];
            if (e.type == -1) continue;
            std::printf("event %zu type %d idx %d pos %.4f %.4f %.4f r %.1f\n", i, e.type, e.index, e.pos[0], e.pos[1], e.pos[2], e.radius);
        }
        for (int i = 0; i < ev.chest_count; ++i) {
            const Chest& c = ev.chests[size_t(i)];
            std::printf("chest %d item %d small %d quality %d pos %.4f %.4f %.4f\n", i, c.item, c.small, c.quality, c.pos[0], c.pos[1], c.pos[2]);
        }
        for (int i = 0; i < ev.atra_count; ++i) std::printf("atra %d id %d pos %.4f %.4f %.4f\n", i, ev.atras[size_t(i)].id, ev.atras[size_t(i)].pos[0], ev.atras[size_t(i)].pos[1], ev.atras[size_t(i)].pos[2]);
        for (int i = 0; i < 3; ++i)
            if (ev.traps[size_t(i)].state) std::printf("trap %d kind %d pos %.4f %.4f %.4f\n", i, ev.traps[size_t(i)].kind, ev.traps[size_t(i)].pos[0], ev.traps[size_t(i)].pos[1], ev.traps[size_t(i)].pos[2]);
        for (const FloorMarker& m : floor_markers(dun, layout))
            std::printf("marker %s ev %d r %.1f cell %d %d pos %.3f %.3f %.3f\n", m.marker.name.c_str(), m.marker.event_id, m.marker.radius, m.cell_x, m.cell_y, m.pos[0], m.pos[1], m.pos[2]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
