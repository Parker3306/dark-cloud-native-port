#include "dccore/dungeon.h"
#include "dccore/gamemath.h"
#include "dccore/placement.h"
#include <cmath>
#include <cstddef>

namespace dccore {

namespace {

using gm::itof;
using gm::fmul;
using gm::fdiv;
using gm::ftoi;
using gm::scaled;
using gm::percent_le;
constexpr float kRandRange = gm::kRandRange;

struct Generator {
    GameRand rng;
    FloorLayout& fl;
    GenState& st;
    Generator(FloorLayout& f, GenState& s) : fl(f), st(s) {}

    // Flat cell access with the game's wrap-around: neighbours of edge cells are read with plain index arithmetic, anything outside the array reads as zero.
    static MapCell& zero_cell() {
        static MapCell z;
        return z;
    }
    MapCell& flat(int i) {
        if (i < 0 || i >= kGrid * kGrid) {
            // Outside the array the game reads whatever lies in memory. The 16x16 scan array of GetRoomLinkInfo ends exactly where the cells begin,
            // so the flags word of cell i < 0 is scan[274 + 19 * i] when that falls inside it; anything else is taken as 0.
            zero_cell() = MapCell{};
            const int k = 274 + 19 * i;
            if (i < 0 && k >= 0 && k < 256) zero_cell().flags = static_cast<uint32_t>(st.scan[size_t(k)]);
            return zero_cell();
        }
        return fl.cell[size_t(i)];
    }
    static int idx(int x, int y) { return y * kGrid + x; }
    MapCell& at(int x, int y) { return flat(idx(x, y)); }

    bool build_room(int x, int y, int w, int h, int room_no) {
        bool free_area = true;
        for (int dy = -1; dy < h + 2; ++dy)
            for (int dx = -1; dx < w + 2; ++dx) {
                int cx = dx + x, cy = dy + y;
                if (cx > -1 && cx < kGrid && cy > -1 && cy < kGrid && fl.cell[size_t(idx(cx, cy))].part != -1) free_area = false;
            }
        if (!free_area) return false;
        for (int dy = 0; dy < h; ++dy)
            for (int dx = 0; dx < w; ++dx) {
                MapCell& c = at(x + dx, y + dy);
                c.part = 0x11;
                c.room[size_t(room_no)] = 1;
                c.flags |= 1;
            }
        return true;
    }

    // One step of a corridor: claims an empty cell, or ends the corridor when it runs into something.
    // Returns true when the corridor is finished.
    bool lay(int x, int y, int room) {
        const int i = idx(x, y);
        if (i < 0 || i >= kGrid * kGrid) return true;
        MapCell& c = fl.cell[size_t(i)];
        if (c.part == -1) {
            c.part = room;
            c.room[size_t(room)] = 1;
            c.flags |= 2;
            return false;
        }
        if ((c.flags & 1) == 1) {
            c.flags |= 4;
            c.room[size_t(room)] = 1;
        } else {
            c.room[size_t(room)] = 1;
        }
        return true;
    }

    void join_room(int a, int b) {
        const RoomRect ra = fl.rooms[size_t(a)], rb = fl.rooms[size_t(b)];
        const int dx = ra.x - rb.x, dy = ra.y - rb.y;
        int dir;
        if (dy * dy < dx * dx) dir = dx < 0 ? 2 : 4;
        else dir = dy < 0 ? 8 : 1;

        int cx = 0, cy = 0, gap = 0;
        auto mark_door = [&](int x, int y) {
            MapCell& c = at(x, y);
            c.flags |= 4;
            c.room[size_t(a)] = 1;
        };
        if (dir == 4) {
            cx = ra.x - 1;
            cy = ra.y + (ra.h >> 1);
            mark_door(ra.x, cy);  // the door flag goes on the room's own edge cell (the decompiler shows it as an offset into the next cell)
            gap = ra.x - (rb.x + rb.w);
        } else if (dir == 2) {
            cx = ra.x + ra.w;
            cy = ra.y + (ra.h >> 1);
            mark_door(cx - 1, cy);
            gap = rb.x - cx;
        } else if (dir == 8) {
            cx = ra.x + (ra.w >> 1);
            cy = ra.y + ra.h;
            mark_door(cx, cy - 1);
            gap = rb.y - cy;
        } else {
            cx = ra.x + (ra.w >> 1);
            cy = ra.y - 1;
            mark_door(cx, ra.y);
            gap = ra.y - (rb.y + rb.h);
        }
        int count = (gap >> 1) + 1;
        if (ra.y == rb.y + rb.h && (dir == 1 || dir == 8) && count < 2) count = 2;
        if (rb.y == ra.y + ra.h && (dir == 1 || dir == 8) && count < 2) count = 2;

        auto step = [&]() {
            if (dir == 4) --cx;
            else if (dir == 2) ++cx;
            else if (dir == 8) ++cy;
            else if (dir == 1) --cy;
        };

        bool done = false;
        int guard = 0;
        while (!done) {
            if (++guard > 4096) break;  // the game would walk off the grid; never happens for generated floors
            if (count == 0) {
                if (dir == 4 || dir == 2) {
                    int span = (rb.y + (rb.h >> 1)) - cy;
                    int dir_y = 1;
                    if (span < 0) { span = -span; dir_y = -1; }
                    for (int k = 0; k < span; ++k) {
                        cy += dir_y;
                        if (lay(cx, cy, a)) done = true;
                    }
                } else {
                    int span = (rb.x + (rb.w >> 1)) - cx;
                    int dir_x = 1;
                    if (span < 0) { span = -span; dir_x = -1; }
                    for (int k = 0; k < span; ++k) {
                        cx += dir_x;
                        if (lay(cx, cy, a)) done = true;
                    }
                }
                step();
                count = 999;
            } else {
                if (lay(cx, cy, a)) {
                    done = true;
                } else {
                    --count;
                    if (count != 0) step();
                }
            }
        }
    }

    void set_room_object() {
        int objects = 0;
        int sx = 0, sy = 0;
        for (size_t r = 0; r < fl.rooms.size(); ++r) {
            const RoomRect& rm = fl.rooms[r];
            if (objects < 2) {
                bool found = false;
                for (int i = 1; i < rm.w - 1; ++i)
                    if ((at(rm.x + i, rm.y).flags & 5) == 1) {
                        sx = i + rm.x;
                        sy = rm.y;
                        found = true;
                    }
                if (percent_le(rng.next(), 30.0f) && found) {
                    at(sx, sy).flags |= 8;
                    ++objects;
                }
            }
            bool found = false;
            for (int j = 1; j < rm.h - 1; ++j)
                for (int i = 1; i < rm.w - 1; ++i)
                    if ((at(rm.x + i, rm.y + j).flags & 0x4d) == 1) {
                        sx = i + rm.x;
                        sy = j + rm.y;
                        found = true;
                    }
            if (percent_le(rng.next(), 30.0f) && found) at(sx, sy).flags |= 0x40;
        }
    }

    void set_room_divid() {
        struct Cand { int x, y, len, kind; };
        for (size_t r = 0; r < fl.rooms.size(); ++r) {
            const RoomRect& rm = fl.rooms[r];
            std::vector<Cand> cands;
            for (int j = 1; j < rm.h - 1; ++j) {
                bool ok = true;
                for (int i = 0; i < rm.w; ++i)
                    if ((at(rm.x + i, rm.y + j).flags & 0x4d) != 1) ok = false;
                if (ok) cands.push_back({rm.x, j + rm.y, rm.w, 0});
            }
            for (int i = 1; i < rm.w - 1; ++i) {
                bool ok = true;
                for (int j = 0; j < rm.h; ++j)
                    if ((at(rm.x + i, rm.y + j).flags & 0x4d) != 1) ok = false;
                if (ok) cands.push_back({i + rm.x, rm.y, rm.h, 1});
            }
            if (cands.empty() || !percent_le(rng.next(), 40.0f)) continue;
            const Cand c = cands[size_t(scaled(int32_t(cands.size()), rng.next()))];
            if (c.kind == 0) {
                for (int i = c.x; i < c.x + c.len; ++i) at(i, c.y).flags |= 0x10;
                int k = scaled(c.len - 2, rng.next());
                fl.cell[size_t(idx(c.x, c.y) + k + 1)].flags |= 0x20;
            } else {
                for (int j = c.y; j < c.y + c.len; ++j) at(c.x, j).flags |= 0x10;
                int k = scaled(c.len - 2, rng.next());
                at(c.x, c.y + k + 1).flags |= 0x20;
            }
        }
    }

    void set_start() {
        std::vector<std::pair<int, int>> list;
        for (int y = 0; y < kGrid; ++y)
            for (int x = 0; x < kGrid; ++x)
                if ((at(x, y).flags & 0x15) == 1 && list.size() < 0x3c) list.emplace_back(x, y);
        for (int tries = 0; tries < 5000; ++tries) {
            int k = scaled(int32_t(list.size()), rng.next());
            if (list.empty()) continue;
            auto [x, y] = list[size_t(k)];
            const int i = idx(x, y);
            if (flat(i - kGrid).flags == 0 && (flat(i + 1).flags & 5) == 1 && (flat(i - 1).flags & 5) == 1) {
                MapCell& c = fl.cell[size_t(i)];
                c.flags = 0x201;
                c.part = 0x24;
                c.rot = 0;
                break;
            }
        }
    }

    void set_stair() {
        for (int pass = 0; pass < 2; ++pass) {
            const int dy = pass == 0 ? -1 : 1;
            std::vector<std::pair<int, int>> list;
            for (int y = 0; y < kGrid; ++y)
                for (int x = 0; x < kGrid; ++x)
                    if ((at(x, y).flags & 2) == 2 && flat(idx(x, y) + dy * kGrid).flags == 0) list.emplace_back(x, y);
            int k = scaled(int32_t(list.size()), rng.next());
            int x = 1, y = 1;
            if (!list.empty()) { x = list[size_t(k)].first; y = list[size_t(k)].second; }
            MapCell& src = at(x, y);
            MapCell& dst = flat(idx(x, y) + dy * kGrid);
            dst.flags = pass == 0 ? 0x80u : 0x100u;
            dst.part = src.part;
            if (src.part >= 0 && src.part < 16) dst.room[size_t(src.part)] = 1;
            dst.rot = pass == 0 ? 0 : 2;
        }
    }

    // Chain tables copied from the game (neighbour mask -> part, rotation). Masks: 1 = up, 2 = right, 4 = left, 8 = down.
    struct Chain { uint32_t mask; int32_t part, rot; };

    static void apply(MapCell& c, const Chain* t, int n, uint32_t mask, bool set_rot) {
        for (int i = 0; i < n; ++i)
            if (t[i].mask == mask) {
                c.part = t[i].part;
                if (set_rot) c.rot = t[i].rot;
            }
    }

    void parts_filter() {
        static const Chain door[] = {{14, 9, 0}, {13, 10, 0}, {7, 11, 0}, {11, 12, 0}};
        static const Chain road[] = {{6, 0, 0}, {9, 0, 1}, {5, 1, 0}, {3, 1, 1}, {10, 1, 2}, {12, 1, 3}, {0, 2, 0}, {1, 3, 0},
                                     {2, 3, 1}, {8, 3, 2}, {4, 3, 3}, {7, 4, 0}, {11, 4, 1}, {14, 4, 2}, {13, 4, 3}};
        static const Chain room[] = {{1, 5, 0}, {2, 6, 0}, {8, 7, 0}, {4, 8, 0}, {5, 13, 0}, {3, 14, 0}, {10, 15, 0}, {12, 16, 0}};
        static const Chain divid[] = {{232, 18, 0}, {212, 19, 0}, {113, 20, 0}, {178, 21, 0}, {249, 26, 0}, {246, 27, 0}};
        static const Chain divid_door[] = {{9, 22, 0}, {6, 23, 0}};

        for (int y = 0; y < kGrid; ++y)
            for (int x = 0; x < kGrid; ++x) {
                const int i = idx(x, y);
                MapCell& c = fl.cell[size_t(i)];
                const MapCell up = flat(i - kGrid), down = flat(i + kGrid), right = flat(i + 1), left = flat(i - 1);

                if ((c.flags & 2) == 2) {
                    uint32_t mask = 0xf;
                    bool b_up = (up.flags & 5) != 1;
                    if ((up.flags & 0x180) != 0 && up.rot != 0) b_up = false;
                    bool b_down = (down.flags & 5) != 1;
                    if ((down.flags & 0x180) != 0 && down.rot != 2) b_down = false;
                    bool b_right = (right.flags & 5) != 1;
                    if ((right.flags & 0x180) != 0 && right.rot != 1) b_right = false;
                    bool b_left = (left.flags & 5) != 1;
                    if ((left.flags & 0x180) != 0 && left.rot != 3) b_left = false;
                    for (int r = 0; r < 16; ++r) {
                        if (c.room[size_t(r)] != 1) continue;
                        if (up.room[size_t(r)] == 1 && b_up) mask &= ~1u;
                        if (down.room[size_t(r)] == 1 && b_down) mask &= ~8u;
                        if (right.room[size_t(r)] == 1 && b_right) mask &= ~2u;
                        if (left.room[size_t(r)] == 1 && b_left) mask &= ~4u;
                    }
                    apply(c, road, 15, mask, true);
                }
                if ((c.flags & 1) == 1) {
                    uint32_t mask = 0xf;
                    const uint32_t n_up = y == 0 ? 0 : (up.flags & 1);
                    const uint32_t n_down = y == kGrid - 1 ? 0 : (down.flags & 1);
                    const uint32_t n_right = x == kGrid - 1 ? 0 : (right.flags & 1);
                    const uint32_t n_left = x == 0 ? 0 : (left.flags & 1);
                    if (n_up == 1) mask = 0xe;
                    if (n_down == 1) mask &= ~8u;
                    if (n_right == 1) mask &= ~2u;
                    if (n_left == 1) mask &= ~4u;
                    apply(c, room, 8, mask, true);
                }
                if ((c.flags & 0x404) == 4) {
                    uint32_t mask = (up.flags & 1) == 1 ? 1u : 0u;
                    if ((down.flags & 1) == 1) mask |= 8;
                    if ((right.flags & 1) == 1) mask |= 2;
                    if ((left.flags & 1) == 1) mask |= 4;
                    apply(c, door, 4, mask, false);
                }
                if ((c.flags & 8) == 8) {
                    c.part = 0x20;
                    c.rot = 0;
                }
                if ((c.flags & 0x40) == 0x40) {
                    c.part = percent_le(rng.next(), 50.0f) ? 0x2d : 0x2e;
                    c.rot = 0;
                }
                const uint32_t f_up = y != 0 ? up.flags : 0, f_down = y != kGrid - 1 ? down.flags : 0, f_right = x != kGrid - 1 ? right.flags : 0, f_left = x != 0 ? left.flags : 0;
                if ((c.flags & 0x10) == 0x10) {
                    uint32_t mask = (f_up & 0x10) == 0x10 ? 1u : 0u;
                    if ((f_down & 0x10) == 0x10) mask |= 8;
                    if ((f_right & 0x10) == 0x10) mask |= 2;
                    if ((f_left & 0x10) == 0x10) mask |= 4;
                    if ((f_up & 1) == 1) mask |= 0x10;
                    if ((f_down & 1) == 1) mask |= 0x80;
                    if ((f_right & 1) == 1) mask |= 0x20;
                    if ((f_left & 1) == 1) mask |= 0x40;
                    apply(c, divid, 6, mask, false);
                }
                if ((c.flags & 0x20) == 0x20) {
                    uint32_t mask = (f_up & 0x10) == 0x10 ? 1u : 0u;
                    if ((f_down & 0x10) == 0x10) mask |= 8;
                    if ((f_right & 0x10) == 0x10) mask |= 2;
                    if ((f_left & 0x10) == 0x10) mask |= 4;
                    apply(c, divid_door, 2, mask, false);
                }
                if ((c.flags & 0x80) == 0x80) c.part = 0x1e;
                if ((c.flags & 0x100) == 0x100) c.part = 0x1f;
                if ((c.flags & 0x200) == 0x200) c.part = 0x28;
            }
    }

    // GetRoomLinkInfo: turns one door into a "link" door (parts 0x24..0x27) whose side is chosen by a flood fill; the fill leaves its marks in the scan array.
    // (The treasure box the game places at the same time is put down by the event builder: GenState::tables makes it draw exactly the game's rand() values; this function is the layout part.)
    void room_link_info() {
        st.scan.fill(-1);
        std::vector<std::pair<int, int>> doors;
        for (int y = 0; y < 16 && doors.size() < 32; ++y)
            for (int x = 0; x < 16; ++x)
                if ((at(x, y).flags & 4) != 0) {
                    doors.emplace_back(x, y);
                    if (doors.size() > 31) break;
                }
        int sx = 0, sy = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                if (at(x, y).part == 0x1e) { sx = x; sy = y; }
        const int pick = scaled(int32_t(doors.size()), rng.next());
        if (doors.empty()) return;
        const int dx = doors[size_t(pick)].first, dy = doors[size_t(pick)].second;
        const int idx16 = dx + dy * 16;
        st.scan[size_t(idx16)] = 9;
        at(dx, dy).flags |= 0x400;
        int toward = (at(dx, dy - 1).flags & 2) == 2 ? 1 : 0;
        if ((at(dx, dy + 1).flags & 2) == 2) toward = 8;
        if ((at(dx + 1, dy).flags & 2) == 2) toward = 2;
        if ((at(dx - 1, dy).flags & 2) == 2) toward = 4;
        int door_part;
        if (toward == 4 || toward == 2) {
            door_part = toward == 4 ? 0x27 : 0x25;
            st.scan[size_t(idx16 + 1)] = 0;
            st.scan[size_t(idx16 - 1)] = 1;
        } else if (toward == 8 || toward == 1) {
            door_part = toward == 8 ? 0x26 : 0x24;
            st.scan[size_t(idx16 - 16)] = 0;
            st.scan[size_t(idx16 + 16)] = 1;
        } else {
            return;
        }
        // flood fill: propagate the region numbers over neighbouring cells of the same room(s)
        uint32_t mask = 0;
        bool changed = true;
        while (changed) {
            changed = false;
            for (int y = 1; y < 15; ++y)
                for (int x = 1; x < 15; ++x) {
                    const int32_t v = st.scan[size_t(x + y * 16)];
                    if (v == -1 || v == 9) continue;
                    const uint32_t f = at(x, y).flags & 7;
                    if (f == 5) mask = 3;
                    else if (f == 1) mask = 5;
                    else if (f == 2) mask = 0x86;
                    const MapCell& c = at(x, y);
                    const int nx[4] = {x, x, x - 1, x + 1}, ny[4] = {y - 1, y + 1, y, y};
                    for (int d = 0; d < 4; ++d) {
                        MapCell& n = at(nx[d], ny[d]);
                        int32_t& sc = st.scan[size_t(nx[d] + ny[d] * 16)];
                        if (n.part == -1 || sc != -1 || (mask & n.flags) == 0) continue;
                        bool shared = false;
                        for (int k = 0; k < 16; ++k)
                            if (c.room[size_t(k)] == 1 && n.room[size_t(k)] == 1) shared = true;
                        if (shared) {
                            sc = v;
                            changed = true;
                        }
                    }
                }
        }
        const int32_t region = st.scan[size_t(sx + (sy + 1) * 16)];
        std::vector<int> choices;
        for (size_t r = 0; r < fl.rooms.size(); ++r) {
            const RoomRect& rm = fl.rooms[r];
            if (region == st.scan[size_t(rm.x + 1 + (rm.y + 1) * 16)]) choices.push_back(int(r));
        }
        if (choices.empty()) return;
        int room_pick = scaled(int32_t(choices.size()), rng.next());   // room for the chest
        if (room_pick < 0 || int(choices.size()) < room_pick) room_pick = 0;
        const RoomRect& chest_room = fl.rooms[size_t(choices[size_t(std::min<int>(room_pick, int(choices.size()) - 1))])];
        if (st.tables) {
            // the first position is accepted: the box list is still empty
            const PutPosition pp = search_put_area(fl, *st.tables, fl.map_no, chest_room.x, chest_room.y, chest_room.w, chest_room.h, rng);
            fl.boxes.push_back({pp.x, pp.y, pp.z});
        } else {
            rng.next(); rng.next(); rng.next();
        }
        MapCell& d = at(dx, dy);
        d.part = door_part;
        d.rot = 0;
    }

    bool place_room(int room_no, int tries_limit, int& tries) {
        tries = 0;
        for (;;) {
            int w = scaled(2, rng.next()) + 3;
            int h = scaled(2, rng.next()) + 3;
            int x = scaled(0x10 - w, rng.next());
            int y = scaled(0x10 - h, rng.next());
            if (build_room(x, y, w, h, room_no)) {
                fl.rooms.push_back({x, y, w, h});
                return true;
            }
            if (++tries > tries_limit - 1) return false;
        }
    }

    void run(int room_count, bool fresh) {
        int tries = 0;
        while (fl.rooms.size() < 2 && tries < 0x40) place_room(int(fl.rooms.size()), 0x40, tries);
        if (fl.rooms.size() >= 2) join_room(0, 1);
        while (int(fl.rooms.size()) <= room_count - 1 && tries < 0x200) {
            if (place_room(int(fl.rooms.size()), 0x200, tries)) {
                const int n = int(fl.rooms.size());
                const RoomRect& nr = fl.rooms[size_t(n - 1)];
                int best = -1;
                int64_t best_d = 10000;
                for (int j = 0; j < n - 1; ++j) {
                    const RoomRect& o = fl.rooms[size_t(j)];
                    const int ddx = (nr.x + (nr.w >> 1)) - (o.x + (o.w >> 1));
                    const int ddy = (nr.y + (nr.h >> 1)) - (o.y + (o.h >> 1));
                    const int64_t d = static_cast<int64_t>(std::sqrt(static_cast<double>(ddx * ddx + ddy * ddy)));
                    if (d < best_d) { best_d = d; best = j; }
                }
                join_room(n - 1, best);
            }
        }
        set_room_object();
        if (!fresh) {
            set_start();
        } else {
            set_room_divid();
            set_start();
            set_stair();
        }
        parts_filter();
    }
};

}  // namespace

void set_dungeon_round_to_zero(bool on) { gm::round_to_zero() = on; }

uint32_t derive_floor_seed(uint32_t rand_state) {
    GameRand r;
    r.state = rand_state;
    const int32_t v = r.next();
    return static_cast<uint32_t>(ftoi(fdiv(itof(v), 100000.0f)));
}

namespace {

// CDungeonMap::CreatPartsList: cells (x, y) inside the 14 x 14 interior that hold part `lo` (and have no link door beside them) or, with hi >= 0, a part in lo..hi.
int creat_parts_list(const FloorLayout& fl, int (*out)[2], int max, int lo, int hi) {
    constexpr int kNb[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    int n = 0;
    for (int y = 1; y < 15; ++y)
        for (int x = 1; x < 15; ++x) {
            bool take = false;
            if (hi == -1) {
                bool free_of_links = true;
                for (const auto& d : kNb) {
                    const MapCell& c = fl.at(x + d[0], y + d[1]);
                    if (c.part != -1 && (c.flags & 0x400) == 0x400) free_of_links = false;
                }
                take = fl.at(x, y).part == lo && free_of_links;
            } else {
                const int part = fl.at(x, y).part;
                take = lo <= part && part <= hi;
            }
            if (!take) continue;
            out[n][0] = x;
            out[n][1] = y;
            ++n;
            if (max - 1 <= n) return n;
        }
    return n;
}

// CDungeonMap::BuildCharaSpecialParts (notes/character_rooms.md sections 2 and 3)
void build_chara_special_parts(FloorLayout& fl, GameRand& rng, const GenState& st) {
    const int map = fl.map_no;
    if (map >= 6) return;
    int list[0x40][2];
    if (0 <= st.chara_state && st.chara_state <= 5) {   // a forced-character floor: SetCharaDoor puts that character's room on the floor (no dice)
        int c = st.chara_state;
        if (c == 1) { rng.next(); c = 0; }   // (both arms of the game's test call SetCharaDoor(0) for the zone 1)
        int n = 0;
        auto put = [&](int part, bool add) {
            const int i = scaled(n, rng.next());
            MapCell& cell = fl.at(list[i][0], list[i][1]);
            cell.part = add ? cell.part + part : part;
        };
        switch (c) {
            case 0: if (st.party_size >= 2 && (n = creat_parts_list(fl, list, 0x40, 0, -1)) > 0) put(0x3b, false); break;
            case 2: if ((n = creat_parts_list(fl, list, 0x40, 9, 12)) > 0) put(0x33, true); break;
            case 3: if ((n = creat_parts_list(fl, list, 0x40, 9, 12)) > 0) put(0x37, true); break;
            case 4: if ((n = creat_parts_list(fl, list, 0x40, 0, -1)) > 0) put(0x44, false); break;
            default: if ((n = creat_parts_list(fl, list, 0x40, 0, -1)) > 0) put(0x45, false); break;
        }
        return;
    }
    int v = ftoi(fdiv(fmul(itof(rng.next()), 100.0f), kRandRange));
    const int floor = st.floor_no, party = st.party_size;
    if (map == 0 && floor < 8) v = 0;
    auto pick = [&](int n) { return scaled(n, rng.next()); };
    auto set_part = [&](int n, int part, bool add) {
        const int i = pick(n);
        MapCell& c = fl.at(list[i][0], list[i][1]);
        c.part = add ? c.part + part : part;
    };
    int n;
    if (party > 1 && v > 0x32 && (n = creat_parts_list(fl, list, 0x40, 0, -1)) > 0) set_part(n, 0x3b, false);
    v = ftoi(fdiv(fmul(itof(rng.next()), 100.0f), kRandRange));
    if (map == 5 || map == 4) {
        if (party < 3 || 9 < v || (n = creat_parts_list(fl, list, 0x40, 9, 12)) < 1) {
            if (party < 4 || 0x13 < v || (n = creat_parts_list(fl, list, 0x40, 9, 12)) < 1) {
                if (party < 5 || 0x1d < v || (n = creat_parts_list(fl, list, 0x40, 0, -1)) < 1) {
                    if (party > 5 && v > 0x28 && (map == 5 || floor > 7) && (n = creat_parts_list(fl, list, 0x40, 0, -1)) > 0) set_part(n, 0x45, false);
                } else {
                    set_part(n, 0x44, false);
                }
            } else {
                set_part(n, 0x37, true);
            }
        } else {
            set_part(n, 0x33, true);
        }
    } else if (map == 3) {
        if (party < 3 || 0x13 < v || (n = creat_parts_list(fl, list, 0x40, 9, 12)) < 1) {
            if (party < 4 || 0x27 < v || (n = creat_parts_list(fl, list, 0x40, 9, 12)) < 1) {
                if (party > 4 && v > 0x27 && floor > 8 && (n = creat_parts_list(fl, list, 0x40, 0, -1)) > 0) set_part(n, 0x44, false);
            } else {
                set_part(n, 0x37, true);
            }
        } else {
            set_part(n, 0x33, true);
        }
    } else if (map == 2) {
        if (party < 3 || 0x27 < v || (n = creat_parts_list(fl, list, 0x40, 9, 12)) < 1) {
            if (party > 3 && v > 0x27 && floor > 8 && (n = creat_parts_list(fl, list, 0x40, 9, 12)) > 0) set_part(n, 0x37, true);
        } else {
            set_part(n, 0x33, true);
        }
    } else if (map == 1 && party > 2 && v > 10 && floor > 8 && (n = creat_parts_list(fl, list, 0x40, 9, 12)) > 0) {
        set_part(n, 0x33, true);
    }
}

}  // namespace

FloorLayout build_random_map(uint32_t seed, int rooms, int map_no, bool fresh, GenState* state) {
    FloorLayout fl;
    fl.map_no = map_no;
    fl.seed = seed;
    GenState local;
    Generator g(fl, state ? *state : local);
    g.rng.seed(seed);
    g.run(rooms, fresh);
    {
        const int roll = ftoi(fdiv(fmul(itof(g.rng.next()), 100.0f), kRandRange));
        if (roll < 0x33 && fresh) g.room_link_info();
    }
    if (fresh) build_chara_special_parts(fl, g.rng, g.st);
    if (map_no == 1) {   // the last pick of buildRandomMap: which of three lighting presets map 1 uses
        const int r = ftoi(fdiv(fmul(itof(g.rng.next()), 3.0f), kRandRange));
        fl.map1_variant = (r < 0 || 2 < r) ? 0 : r;
    }
    for (int y = 0; y < kGrid; ++y) {  // SetUnderLoad: the last column is filled with a blank filler part
        fl.at(kGrid - 1, y).part = 0x2c;
        fl.at(kGrid - 1, y).rot = 0;
    }
    fl.rand_state = g.rng.state;
    return fl;
}

}  // namespace dccore
