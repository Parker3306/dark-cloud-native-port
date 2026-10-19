#include "dccore/monsters.h"
#include <cmath>
#include "dccore/gamemath.h"
#include "dccore/pack.h"
#include "dccore/placement.h"

namespace dccore {

namespace {
float dist3(const float* a, const float* b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
}  // namespace

void MonsterUnit::load_floor(const Disc& disc, const GameTables& tables, int map_no, int floor, bool back_floor) {
    std::vector<int> ids;
    const auto& rows = tables.layout[back_floor ? 1 : 0][size_t(map_no)];
    if (floor >= 0 && size_t(floor) < rows.size())
        for (const LayoutSlot& slot : rows[size_t(floor)]) {
            if (slot.monster < 0 || size_t(slot.monster) >= tables.monsters.size()) break;
            ids.push_back(slot.monster);
        }
    load_ids(disc, tables, ids);
}

void MonsterUnit::load_ids(const Disc& disc, const GameTables& tables, const std::vector<int>& ids) {
    bases.clear();
    for (auto& s : slots) s = MonsterInstance{};
    for (int id : ids) {
        if (id < 0 || size_t(id) >= tables.monsters.size()) break;
        MonsterBase b;
        b.monster_id = id;
        b.entry = tables.monsters[size_t(id)];
        const std::string dir = "dun/monstor/";
        b.model = std::make_shared<Character>(load_character(Pack(disc.read(dir + b.entry.stem() + ".chr"))));
        for (int i = 0; i < 3; ++i) {
            const std::string x = b.entry.extra_stem(i);
            if (!x.empty() && disc.contains(dir + x + ".chr")) b.extras.push_back(std::make_shared<Character>(load_character(Pack(disc.read(dir + x + ".chr")))));
        }
        const std::string script = dir + b.entry.script_stem() + ".stb";
        if (disc.contains(script)) b.script = std::make_shared<ScriptProgram>(disc.read(script));
        for (int k = 0; k < 2; ++k) {
            const int id = b.entry.s16(0x68 + 2 * size_t(k));
            if (id < 0 || size_t(id) >= tables.shot_defs.size()) continue;
            const ShotDef& d = tables.shot_defs[size_t(id)];
            const std::string path = "dun/effect/" + d.name + ".chr";
            if (!disc.contains(path)) continue;
            b.shot_def[size_t(k)] = id;
            b.shot[size_t(k)] = d;
            b.shot_model[size_t(k)] = std::make_shared<Character>(load_character(Pack(disc.read(path))));
        }
        bases.push_back(std::move(b));
    }
}

int MonsterUnit::active_count() const {
    int n = 0;
    for (const auto& s : slots) n += s.active ? 1 : 0;
    return n;
}

void MonsterUnit::arrange(const FloorLayout& layout, const GameTables& tables, int map_no, GameRand& rng, int count, int which, const std::function<void(int)>& on_place, const FloorEvents* events) {
    using namespace gm;
    bool used[16] = {};
    for (int n = 0; n < count; ++n) {
        bool done = false;
        int tries = 0;
        while (!done) {
            ++tries;
            if (64999 < tries) done = true;
            const PutPosition pp = search_put_area(layout, tables, map_no, 0, 0, kGrid, kGrid, rng);
            const float pos[3] = {pp.x, pp.y, pp.z};
            bool near_box = false;
            if (events) {   // CheckTreasureBox, CheckAtra and CheckTrapCircle with a distance of 20
                const float pos4[4] = {pos[0], pos[1], pos[2], 1.0f};
                near_box = events->chest_near(pos4, 20.0f) || events->atra_near(map_no, pos4, 20.0f) || events->trap_near(pos4, 20.0f);
            } else {
                for (const auto& b : layout.boxes)
                    if (dist3(b.data(), pos) < 20.0f) near_box = true;
            }
            if (arrange_log) arrange_log->push_back("candidate " + std::to_string(pos[0]) + " " + std::to_string(pos[1]) + " " + std::to_string(pos[2]) + (near_box ? " near box" : ""));
            if (near_box) continue;   // (the chests of the floor; the atra statues and trap circles are put down after the monsters)
            bool too_close = false;
            for (const auto& s : slots)
                if (s.active && dist3(s.pos, pos) <= 25.0f) too_close = true;
            if (too_close) continue;
            int crowd = 0;
            for (const auto& s : slots)
                if (s.active && dist3(s.pos, pos) <= 480.0f) ++crowd;
            if (crowd >= 3) continue;
            int pick = which;
            if (which == -1) {
                pick = scaled(int32_t(bases.size()), rng.next());
                if (pick < 0 || pick >= int(bases.size())) pick = 0;
                const int kind = bases.empty() ? 0 : bases[size_t(pick)].entry.kind();
                if (kind != 0 && kind != 3) {
                    if (used[pick]) continue;
                    used[pick] = true;
                }
            }
            if (bases.empty()) return;
            int slot = -1;
            for (int i = 0; i < kSlots; ++i)
                if (!slots[size_t(i)].active) { slot = i; break; }
            if (slot < 0) return;
            MonsterInstance& m = slots[size_t(slot)];
            m.active = true;
            m.base = pick;
            m.pos[0] = pos[0]; m.pos[1] = pos[1]; m.pos[2] = pos[2];
            m.yaw = 0;
            if (on_place) on_place(slot);
            done = true;
        }
    }
}

}  // namespace dccore
