// dcback <iso> <stem> <state> <rooms> <map_no> <floor> [--status file] [--scan s,s,..] [--arrange-main n] [--arrange-back n]
// One BtCleatRandomMap as the port runs it (DcModels::start_floor): the generator is at <state> (srand(state) in the live rig), the main floor and
// its events are built, then the back floor and its events (the stream just continues), then the main floor's 15 monsters, then (the swap) the back floor's 8.
// Output blocks: "main" / "back" with "cells" (part rot per cell), event / chest / atra / trap lines, and "mon" lines (slot, model, position).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/events.h"
#include "dccore/floor.h"
#include "dccore/monster_ai.h"
#include "dccore/monsters.h"
#include "dccore/pack.h"
#include "dccore/walk.h"

using namespace dccore;

static void print_floor(const char* tag, const FloorLayout& layout, const FloorEvents& ev) {
    std::printf("%s seed %u rand_after_map %u\n", tag, layout.seed, layout.rand_state);
    std::printf("%s rooms", tag);
    for (const auto& r : layout.rooms) std::printf(" [%d %d %d %d]", r.x, r.y, r.w, r.h);
    std::printf("\n");
    std::printf("%s cells", tag);
    for (const auto& c : layout.cell) std::printf(" %d:%d", c.part, c.rot);
    std::printf("\n");
    for (size_t i = 0; i < ev.events.size(); ++i) {
        const FloorEvent& e = ev.events[i];
        if (e.type == -1) continue;
        std::printf("%s event %zu type %d idx %d pos %.4f %.4f %.4f\n", tag, i, e.type, e.index, e.pos[0], e.pos[1], e.pos[2]);
    }
    for (int i = 0; i < ev.chest_count; ++i) {
        const Chest& c = ev.chests[size_t(i)];
        std::printf("%s chest %d item %d small %d quality %d pos %.4f %.4f %.4f\n", tag, i, c.item, c.small, c.quality, c.pos[0], c.pos[1], c.pos[2]);
    }
    for (int i = 0; i < ev.atra_count; ++i) std::printf("%s atra %d id %d pos %.4f %.4f %.4f\n", tag, i, ev.atras[size_t(i)].id, ev.atras[size_t(i)].pos[0], ev.atras[size_t(i)].pos[1], ev.atras[size_t(i)].pos[2]);
    for (int i = 0; i < 3; ++i)
        if (ev.traps[size_t(i)].state) std::printf("%s trap %d kind %d pos %.4f %.4f %.4f\n", tag, i, ev.traps[size_t(i)].kind, ev.traps[size_t(i)].pos[0], ev.traps[size_t(i)].pos[1], ev.traps[size_t(i)].pos[2]);
}

int main(int argc, char** argv) {
    if (argc < 7) { std::fprintf(stderr, "usage: dcback <iso> <stem> <state> <rooms> <map_no> <floor> [options]\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const std::string stem = argv[2];
        const int rooms = std::atoi(argv[4]), map_no = std::atoi(argv[5]), floor = std::atoi(argv[6]);
        Bytes cfg = disc.read("dun/" + stem + ".cfg");
        Pack pack(disc.read("dun/mpd_pack/" + stem + ".mpd"));
        Dungeon dun = load_dungeon(std::string(cfg.begin(), cfg.end()), pack);
        Elf elf(disc.read_root("SCUS_971.11"));
        GameTables tables = GameTables::load(elf);
        EventTables et = EventTables::load(elf);
        UserStatus status = UserStatus::new_game();
        long arrange_main = -1, arrange_back = -1;
        int party_override = -999, chara_override = -999;
        GenState gen;
        gen.tables = &tables;
        for (int i = 7; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--status") && i + 1 < argc) {
                std::ifstream f(argv[++i], std::ios::binary);
                f.read(reinterpret_cast<char*>(status.raw.data()), std::streamsize(status.raw.size()));
            } else if (!std::strcmp(argv[i], "--scan") && i + 1 < argc) {
                const char* q = argv[++i];
                char* e = nullptr;
                for (size_t k = 0; k < gen.scan.size() && *q; ++k) {
                    gen.scan[k] = int32_t(std::strtol(q, &e, 10));
                    q = e;
                    if (*q == ',') ++q;
                }
            } else if (!std::strcmp(argv[i], "--arrange-main") && i + 1 < argc) arrange_main = long(std::strtoul(argv[++i], nullptr, 0));
            else if (!std::strcmp(argv[i], "--arrange-back") && i + 1 < argc) arrange_back = long(std::strtoul(argv[++i], nullptr, 0));
            else if (!std::strcmp(argv[i], "--party") && i + 1 < argc) party_override = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--chara-state") && i + 1 < argc) chara_override = std::atoi(argv[++i]);
        }
        gen.floor_no = floor;
        gen.party_size = status.get<int8_t>(5);
        gen.chara_state = status.get<int32_t>(0x8b10);
        if (party_override != -999) gen.party_size = party_override;
        if (chara_override != -999) gen.chara_state = chara_override;
        GameRand rng;
        rng.seed(uint32_t(std::strtoul(argv[3], nullptr, 0)));
        const uint32_t seed = derive_floor_seed(rng.state);
        rng.next();
        FloorLayout layout = build_random_map(seed, rooms, map_no, true, &gen);
        rng.seed(layout.rand_state);
        FloorEvents events = make_floor_events(layout, map_no);
        build_event_data(events, layout, tables, et, status, map_no, floor, true, rng);
        FloorLayout ura = build_random_map(rng.state, rooms, map_no, false, &gen);
        rng.seed(ura.rand_state);
        FloorEvents ura_events = make_floor_events(ura, map_no);
        build_event_data(ura_events, ura, tables, et, status, map_no, floor, false, rng);
        for (int pass = 0; pass < 2; ++pass) {
            const bool back = pass == 1;
            const char* tag = back ? "back" : "main";
            FloorLayout& lay = back ? ura : layout;
            FloorEvents& evs = back ? ura_events : events;
            MonsterUnit unit;
            unit.load_floor(disc, tables, map_no, floor, back);
            if (back ? arrange_back >= 0 : arrange_main >= 0) rng.seed(uint32_t(back ? arrange_back : arrange_main));
            std::printf("%s rand_before_arrange %u models", tag, rng.state);
            for (const auto& b : unit.bases) std::printf(" %d", b.monster_id);
            std::printf("\n");
            Walker walker(&dun, &lay);
            walker.place_at_start();
            MonsterWorld world(&dun, &lay, &unit, &rng);
            world.set_monster_shape(parse_collision_mds(disc.read("dun/etc/atrcol.mds")));
            world.player.pos[0] = walker.player.pos[0]; world.player.pos[1] = walker.player.pos[1]; world.player.pos[2] = walker.player.pos[2];
            world.double_weapon_exp = back;
            unit.arrange(lay, tables, map_no, rng, back ? 8 : 15, -1, [&](int slot) {
                world.spawn(slot);
                for (int k = 0; k < 3; ++k) unit.slots[size_t(slot)].pos[k] = world.actors[size_t(slot)].pos[k];
            }, &evs);
            for (int s = 0; s < MonsterUnit::kSlots; ++s) {   // RsetMimicEvent: the monsters that are mimics wait inside a chest (kind 3 small, 4 big)
                const MonsterActor& a = world.actors[size_t(s)];
                if (a.state == -1 || a.monster_code != 8 || a.active != 0) continue;
                const MonsterInstance& mi = unit.slots[size_t(s)];
                if (mi.base < 0 || size_t(mi.base) >= unit.bases.size()) continue;
                const int kind = unit.bases[size_t(mi.base)].entry.kind();
                if (kind != 3 && kind != 4) continue;
                const float p[4] = {a.pos[0], a.pos[1], a.pos[2], 1.0f};
                evs.set_mimic(p, kind == 3, s);
            }
            print_floor(tag, lay, evs);
            for (int s = 0; s < MonsterUnit::kSlots; ++s) {
                const MonsterActor& a = world.actors[size_t(s)];
                if (a.slot < 0) continue;
                std::printf("%s mon %d model %d hp %d pos %.3f %.3f %.3f\n", tag, s, unit.bases[size_t(a.base)].monster_id, a.hp, a.pos[0], a.pos[1], a.pos[2]);
            }
            std::printf("%s rand_after_arrange %u\n", tag, rng.state);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
