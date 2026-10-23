#include "dccore/events.h"
#include <algorithm>
#include <cstring>
#include "dccore/gamemath.h"
#include "dccore/vu0.h"

namespace dccore {

namespace {
constexpr uint32_t kPieroItemListPtr = 0x279CF0, kItemPutListPtr = 0x2763D0, kItemSetRateTbl = 0x279D30, kFloorNum = 0x279D70, kNoEntryTbl = 0x279E00, kCenterFloorTbl = 0x279E20;
constexpr uint32_t kMaxFloorTbl = 0x26B000, kAtraAppearData = 0x25DF30;
constexpr uint32_t kStride = 0x208;

void copy_vec(float* dst, const float* src) { for (int i = 0; i < 4; ++i) dst[i] = src[i]; }
}  // namespace

EventTables EventTables::load(const Elf& elf) {
    EventTables t;
    for (int i = 0; i < 14; ++i) {
        const uint32_t base = elf.get<uint32_t>(kItemPutListPtr + 4u * uint32_t(i));
        for (uint32_t r = 0; r < 0x80; ++r) {
            const uint32_t a = base + r * kStride;
            PutListRecord rec;
            rec.key = elf.get<int32_t>(a);
            if (rec.key == -1) break;
            for (uint32_t k = 2; k < kStride / 4; ++k) {
                const int32_t id = elf.get<int32_t>(a + 4 * k);
                if (id == -1) break;
                rec.ids.push_back(id);
            }
            t.put_lists[i].push_back(std::move(rec));
        }
    }
    for (int i = 0; i < 14; ++i) {
        const uint32_t base = elf.get<uint32_t>(kPieroItemListPtr + 4u * uint32_t(i));
        for (uint32_t k = 0; k < 0x104; ++k) t.piero[i][k] = elf.get<int32_t>(base + 4 * k);
    }
    for (int m = 0; m < 7; ++m) {
        t.floor_num[m] = elf.get<int32_t>(kFloorNum + 4u * uint32_t(m));
        t.max_floor[m] = elf.get<int32_t>(kMaxFloorTbl + 4u * uint32_t(m));
        const View v = elf.at(kItemSetRateTbl + 4u * uint32_t(m), 380);
        std::memcpy(t.rate_bytes[m].data(), v.data(), 380);
    }
    for (int m = 0; m < 6; ++m) {
        t.center_floor[m] = elf.get<int32_t>(kCenterFloorTbl + 4u * uint32_t(m));
        const uint32_t ap = elf.get<uint32_t>(kAtraAppearData + 4u * uint32_t(m));
        for (uint32_t i = 0;; ++i) {
            const int32_t id = elf.get<int32_t>(ap + 12 * i);
            if (id == -1) break;
            t.atra_appear[m].push_back({id, elf.get<int32_t>(ap + 12 * i + 4), elf.get<int32_t>(ap + 12 * i + 8)});
        }
        const uint32_t ne = elf.get<uint32_t>(kNoEntryTbl + 4u * uint32_t(m));
        for (uint32_t i = 0;; ++i) {
            const int32_t f = elf.get<int32_t>(ne + 4 * i);
            if (f == -1) break;
            t.no_entry[m].push_back(f);
        }
    }
    return t;
}

namespace {
struct StackRate { int id; int16_t rate; };
// Recorded with tools/dc/probe_preset.py (see events.h). (19424, 476) = 0x01DC4BE0 = MainDungeonMap, (22256, 477) = 0x01DD56F0 = UraDungeonMap, (-1932, 511) = a stack address.
constexpr StackRate kStackRatesMain[] = {
    {193, 1}, {200, 16256}, {201, 19424}, {202, 476}, {203, -1280}, {204, 36}, {205, -708}, {206, 36}, {207, -16540}, {208, 41}, {209, 20}, {213, 26968}, {214, 41}, {217, -16520},
    {218, 28}, {229, -1932}, {230, 511}, {231, -1928}, {232, 511}, {239, 6}, {241, -708}, {242, 36}, {249, -16560}, {250, 41}, {257, -1528}, {269, -1}, {270, -1}, {271, -1},
    {272, -1}, {273, 1}, {277, -1}, {278, -1}, {279, -1}, {280, -1}, {281, 19424}, {282, 476}, {285, -1}, {286, -1}, {287, -1}, {288, -1}, {293, -1}, {294, -1}, {295, -1},
    {296, -1}, {301, -1}, {302, -1}, {303, -1}, {304, -1}, {305, 6}, {309, -1}, {310, -1}, {311, -1}, {312, -1}, {325, -1}, {326, -1}, {327, -1}, {328, -1}, {333, -1},
    {334, -1}, {335, -1}, {336, -1}, {337, 1}, {361, 1}, {369, 2}, {377, 19424}, {378, 476}, {393, -31128}, {394, 28}};
constexpr StackRate kStackRatesBack[] = {
    {203, -1280}, {204, 36}, {205, -708}, {206, 36}, {207, -16540}, {208, 41}, {209, 20}, {213, 26968}, {214, 41}, {229, -1932}, {230, 511}, {231, -1928}, {232, 511},
    {241, -708}, {242, 36}, {249, -16560}, {250, 41}, {257, -1528}, {281, 22256}, {282, 477}, {305, 6}, {369, 2}, {377, 22256}, {378, 477}, {385, 1}, {389, 2570},
    {390, 2570}, {391, 2570}, {392, 2570}, {393, -31128}, {394, 28}};
}  // namespace

int EventTables::stack_rate(int id, bool back) const {
    if (back) {
        for (const StackRate& r : kStackRatesBack)
            if (r.id == id) return r.rate;
    } else {
        for (const StackRate& r : kStackRatesMain)
            if (r.id == id) return r.rate;
    }
    return 0;
}

int EventTables::rate(int map, int id) const {
    if (id < 1 || id > 190 || map < 0 || map > 6) return 0;
    int16_t v;
    std::memcpy(&v, rate_bytes[map].data() + size_t(id - 1) * 2, 2);
    return v;
}

int EventTables::preset_item(int map, int floor, int table, bool small, GameRand& rng) const {
    const std::vector<PutListRecord>& recs = put_lists[map + table * 7];
    int key = -1;
    for (const PutListRecord& r : recs)
        if (floor + 1 == r.key) key = floor + 1;
    if (key == -1) key = floor < floor_num[map] ? 0x100 : 0xff;
    const PutListRecord* rec = nullptr;
    for (const PutListRecord& r : recs)
        if (r.key == key) { rec = &r; break; }
    if (!rec) return -1;
    std::vector<int> pool;
    for (int id : rec->ids) {
        if (small ? (0x50 < id && id < 0x101) : (0x100 < id)) pool.push_back(id);
    }
    if (pool.empty()) return -1;
    const int n = int(pool.size());
    for (int tries = 0; tries < (small ? 0xffff : 0xfffe); ++tries) {
        const int32_t r1 = rng.next();
        const float f = gm::scaledf(gm::itof(n), r1);
        int idx = gm::ftoi(f);
        if (0.0f < gm::fsub(f, gm::itof(idx))) ++idx;
        if (idx < 0 || n <= idx) idx = 0;
        const int32_t r2 = rng.next();
        const int16_t roll = int16_t(gm::ftoi(gm::fdiv(gm::fmul(gm::itof(r2), 100.0f), gm::kRandRange)));
        const int id = pool[size_t(idx)];
        if ((id > 190 ? stack_rate(id, table != 0) : rate(map, id)) < roll) return id;
    }
    return -1;
}

void EventTables::piero_item(int map, int back, int floor, GameRand& rng, int out[2]) const {
    const std::array<int32_t, 0x104>& p = piero[size_t(map + back * 7)];
    const bool shallow = floor < floor_num[map];
    const int counts[2] = {p[shallow ? 0 : 0x82], p[shallow ? 1 : 0x83]};
    const int offsets[2] = {shallow ? 2 : 0x84, shallow ? 0x42 : 0xc4};
    for (int k = 0; k < 2; ++k) {
        int item = -1;
        for (int guard = 0; guard < 100000 && item == -1; ++guard) {
            const int32_t r1 = rng.next();
            int idx = gm::ftoi(gm::scaledf(gm::itof(counts[k]), r1));
            if (counts[k] <= idx) idx = 0;
            const int32_t r2 = rng.next();
            const int16_t roll = int16_t(gm::ftoi(gm::fdiv(gm::fmul(gm::itof(r2), 100.0f), gm::kRandRange)));
            const int id = p[size_t(offsets[k] + idx)];
            if (roll <= rate(map, id)) continue;   // rejected: draw again
            item = id;
        }
        out[k] = item;
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

int FloorEvents::free_event() const {
    for (int i = 0; i < int(events.size()); ++i)
        if (events[size_t(i)].type == -1) return i;
    return -1;
}

int FloorEvents::set_chest(const float pos[4], int item, bool small, int quality) {
    int slot = -1;
    for (int i = 0; i < int(chests.size()); ++i)
        if (!chests[size_t(i)].active) { slot = i; break; }
    if (slot == -1) return -1;
    Chest& c = chests[size_t(slot)];
    copy_vec(c.pos, pos);
    c.active = true;
    c.state = 0;
    c.small = small ? 1 : 0;
    c.item = item;
    c.quality = quality;
    const int e = free_event();
    if (e == -1) return -1;
    FloorEvent& ev = events[size_t(e)];
    ev.type = 2;
    ev.state = 0;
    ev.index = chest_count;
    copy_vec(ev.pos, pos);
    ev.pos[2] = gm::fadd(pos[2], small ? 8.0f : 10.0f);   // the event sits 8 (small) or 10 (big) units in front of the chest
    ev.radius = 8.0f;
    ++chest_count;
    return e;
}

int FloorEvents::set_mimic(const float pos[4], bool small, int monster_slot) {
    int slot = -1;
    for (int i = 0; i < int(chests.size()); ++i)
        if (!chests[size_t(i)].active) { slot = i; break; }
    if (slot == -1) return -1;
    const int e = free_event();
    if (e == -1) return -1;
    Chest& c = chests[size_t(slot)];
    copy_vec(c.pos, pos);
    c.active = true;
    c.state = 0;
    c.small = small ? 1 : 0;
    c.item = monster_slot;   // where a chest keeps its item id: the monster inside
    FloorEvent& ev = events[size_t(e)];
    ev.type = 8;
    ev.state = 0;
    ev.index = slot;
    copy_vec(ev.pos, pos);
    ev.pos[2] = gm::fadd(pos[2], small ? 8.0f : 10.0f);
    ev.radius = 8.0f;
    if (slot >= chest_count) chest_count = slot + 1;
    return e;
}

void FloorEvents::set_atra(const float pos[4], int atra_id) {
    AtraBall& a = atras[size_t(atra_count)];
    copy_vec(a.pos, pos);
    a.state = 0;
    a.id = atra_id;
    a.active = true;
    const int e = free_event();
    if (e == -1) return;
    FloorEvent& ev = events[size_t(e)];
    ev.type = 3;
    ev.state = 0;
    ev.index = atra_count;
    copy_vec(ev.pos, pos);
    ev.radius = 13.0f;
    ++atra_count;
}

bool FloorEvents::chest_near(const float pos[4], float dist) const {
    for (int i = 0; i < chest_count; ++i)
        if (chests[size_t(i)].active && vu0::dist(chests[size_t(i)].pos, pos) < dist) return true;
    return false;
}

bool FloorEvents::atra_near(int map, const float pos[4], float dist) const {
    if (map >= 6) return false;
    for (int i = 0; i < atra_count; ++i)
        if (atras[size_t(i)].active && vu0::dist(atras[size_t(i)].pos, pos) < dist) return true;
    return false;
}

bool FloorEvents::trap_near(const float pos[4], float dist) const {
    for (const TrapCircle& t : traps)
        if (t.state != 0 && vu0::dist(t.pos, pos) < dist) return true;
    return false;
}

int FloorEvents::active_event(const float player[4]) const {
    for (int i = 0; i < int(events.size()); ++i) {
        const FloorEvent& e = events[size_t(i)];
        if (e.type != -1 && vu0::dist(e.pos, player) <= e.radius) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

namespace {
size_t floor_list(int map, int floor) { return 0x278 + size_t(map) * 0x500 + size_t(floor) * 0x20; }
int max_atra_num(const UserStatus& s, int map, int floor) {
    if (map == 6) return 0;
    int n = 0;
    for (int i = 0; i < 8; ++i)
        if (s.get<int32_t>(floor_list(map, floor) + size_t(i) * 4) != -1) ++n;
    return n;
}
void set_get_atra(UserStatus& s, int map, int floor, int value) {
    if (map == 6) return;
    for (int i = 0; i < 8; ++i) {
        const size_t at = floor_list(map, floor) + size_t(i) * 4;
        if (s.get<int32_t>(at) == -1) { s.set<int32_t>(at, value); return; }
    }
}
bool atra_floor_allowed(const EventTables& t, int map, int floor_1based) {
    if (map >= 6) return false;
    for (int f : t.no_entry[map])
        if (f == floor_1based) return false;
    return true;
}
}  // namespace

int atra_list_count(const UserStatus& s, int map, int floor) { return max_atra_num(s, map, floor); }

void atra_list_make(UserStatus& s, const EventTables& t, int map, GameRand& rng) {
    if (map >= 6) return;
    const auto& list = t.atra_appear[map];
    const int center = t.center_floor[map], last = t.max_floor[map];
    int early = 0, late = 0;
    for (size_t i = 0; i < list.size(); ++i) {
        const int b = list[i][1];
        if (b == -1) early += list[i][2];
        if (b == -2) late += list[i][2];
        if (b != -1 && b != -2) set_get_atra(s, map, b - 1, int(i));
    }
    for (int i = 0; i < early; ++i) {
        for (;;) {
            const int floor = gm::scaled(center - 1, rng.next());
            if (max_atra_num(s, map, floor) < 8 && atra_floor_allowed(t, map, floor + 1)) { set_get_atra(s, map, floor, -2); break; }
        }
    }
    for (int i = 0; i < late; ++i) {
        for (;;) {
            const int floor = gm::scaled((last - center) - 1, rng.next()) + center;
            if (max_atra_num(s, map, floor) < 8 && atra_floor_allowed(t, map, floor + 1)) { set_get_atra(s, map, floor, -2); break; }
        }
    }
    for (size_t i = 0; i < list.size(); ++i) {
        const size_t at = 0x2078 + size_t(map) * 0x4b0 + i * 0xc;
        s.set<int32_t>(at, list[i][0]);
        s.set<int32_t>(at + 4, list[i][1]);
        s.set<int32_t>(at + 8, list[i][2]);
    }
    s.set<int8_t>(0x42bf + size_t(map), 0);   // the table now exists
}

int atra_floor_choice(const UserStatus& s, const EventTables& t, int map, int floor, int out[8], GameRand& rng) {
    if (map >= 6) return 0;
    int tbl[100][3];
    for (int i = 0; i < 100; ++i)
        for (int k = 0; k < 3; ++k) tbl[i][k] = s.get<int32_t>(0x2078 + size_t(map) * 0x4b0 + size_t(i) * 0xc + size_t(k) * 4);
    for (int i = 0; i < 8; ++i) out[i] = s.get<int32_t>(floor_list(map, floor) + size_t(i) * 4);
    const int want = (t.center_floor[map] - 1 < floor) ? -2 : -1;
    int count = 0;
    for (int i = 0; i < 8; ++i) {
        if (out[i] < 0) {
            if (out[i] == -2) {
                for (;;) {
                    const int idx = gm::scaled(100, rng.next());
                    if (tbl[idx][0] != -1 && tbl[idx][2] > 0 && want == tbl[idx][1]) {
                        out[i] = idx;
                        --tbl[idx][2];
                        break;
                    }
                }
                ++count;
            }
        } else {
            ++count;
        }
    }
    int packed[8];
    for (int& p : packed) p = -1;
    int n = 0;
    for (int i = 0; i < 8; ++i)
        if (out[i] >= 0) packed[n++] = out[i];
    for (int i = 0; i < 8; ++i) out[i] = packed[i];
    return count;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

FloorEvents make_floor_events(const FloorLayout& layout, int map) {
    FloorEvents ev;
    for (const auto& b : layout.boxes) {
        const float pos[4] = {b[0], b[1], b[2], 1.0f};
        ev.set_chest(pos, 0xd8 + map, true, 0);
    }
    return ev;
}

void build_event_data(FloorEvents& ev, const FloorLayout& layout, const GameTables& tables, const EventTables& et, const UserStatus& status, int map, int floor, bool main_floor, GameRand& rng) {
    using gm::scaled;
    const bool back = !main_floor;   // param_4 == 0
    const int table = back ? 1 : 0;
    auto free_spot = [&](float out[4]) {
        const PutPosition p = search_put_area(layout, tables, map, 0, 0, 20, 20, rng);
        out[0] = p.x; out[1] = p.y; out[2] = p.z; out[3] = p.w;
        return p;
    };
    auto usable = [&](const float pos[4]) { return !ev.chest_near(pos, 20.0f) && !ev.atra_near(map, pos, 20.0f) && !ev.trap_near(pos, 20.0f); };
    int placed = 0;
    const int target = scaled(4, rng.next()) + 1;
    float pos[4];
    while (placed < target + 3) {
        free_spot(pos);
        if (!usable(pos)) continue;
        if (gm::fdiv(gm::fmul(gm::itof(rng.next()), 100.0f), gm::kRandRange) < 88.0f || placed < 2) {
            int item = et.preset_item(map, floor, table, true, rng);
            if (!back) {
                if (placed == 0) item = 0xe9;
                if (placed == 1) item = 0xea;
            }
            if (item != -1) {
                ev.set_chest(pos, item, true, 0);
                ++placed;
            }
        } else {
            const int item = et.preset_item(map, floor, table, false, rng);
            if (item != -1) {
                int q = scaled(6, rng.next());
                if (q < 0 || 5 < q) q = 1;
                ev.set_chest(pos, item, false, q);
                ++placed;
            }
        }
    }
    for (TrapCircle& t : ev.traps) t.state = 0;   // initTrapCircle
    for (int i = 0; i < 3; ++i) {
        if (gm::ftoi(gm::fdiv(gm::fmul(gm::itof(rng.next()), 100.0f), gm::kRandRange)) < 0x15) {
            float p2[4];
            free_spot(p2);
            if (usable(p2)) {
                for (TrapCircle& t : ev.traps) {   // SetupTrapCircle
                    if (t.state != 0) continue;
                    copy_vec(t.pos, p2);
                    t.state = 1;
                    t.kind = std::min(scaled(10, rng.next()), 9);
                    t.timer = 0;
                    break;
                }
            }
        }
    }
    if (!back) {
        int ids[8];
        const int n = atra_floor_choice(status, et, map, floor, ids, rng);
        int placed_atra = 0;
        while (placed_atra < n) {
            float p3[4];
            free_spot(p3);
            if (!usable(p3)) continue;
            ev.set_atra(p3, ids[placed_atra]);
            ++placed_atra;
        }
    }
}

}  // namespace dccore
