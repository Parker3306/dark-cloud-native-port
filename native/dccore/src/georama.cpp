#include "dccore/georama.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <set>
#include "dccore/edit.h"

namespace dccore {

GeoTables GeoTables::load(const Elf& elf) {
    GeoTables t;
    auto cstring = [&](uint32_t va) { return va ? elf.cstring(va, 64) : std::string(); };
    for (int town = 0; town < 6; ++town) {
        for (int p = 0; p < 24; ++p) {
            const uint32_t base = 0x2540D0u + uint32_t(town) * 0x125c + uint32_t(p) * 0xbc;
            int32_t w[47];
            std::memcpy(w, elf.at(base, sizeof w).data(), sizeof w);
            GeoPartDef& d = t.parts[town][p];
            d.max = w[0]; d.stackable = w[1]; d.plate = w[2]; d.name_msg = w[3]; d.comp_kind = w[4];
            for (int k = 0; k < 6; ++k) {
                const int b = 5 + 7 * k;
                GeoElemDef& e = d.elem[k];
                e.id = w[b]; e.rel = w[b + 1]; e.npc = w[b + 6];
                for (int n = 0; n < 4; ++n) e.names[n] = cstring(uint32_t(w[b + 2 + n]));
            }
        }
        for (int i = 0; i < 100; ++i) {
            const uint32_t base = 0x25AF00u + uint32_t(town) * 0x4b0 + uint32_t(i) * 12;
            GeoElemRow& r = t.elem[town][i];
            r.tile = elf.get<int32_t>(base);
            r.msg = elf.get<int32_t>(base + 4);
            r.copies = elf.get<int32_t>(base + 8);
        }
        if (town < 5)
            for (int i = 0; i < 20; ++i) {
                const uint32_t base = 0x267070u + uint32_t(town) * 400 + uint32_t(i) * 0x14;
                GeoCharaRow& r = t.chara[town][i];
                r.model = cstring(elf.get<uint32_t>(base));
                r.part = elf.get<int16_t>(base + 4);
                r.element = elf.get<int16_t>(base + 6);
                r.moves = elf.get<int16_t>(base + 8);
                r.speed = elf.get<float>(base + 12);
                r.hide_until = elf.get<int16_t>(base + 16);
            }
        const uint32_t list = elf.get<uint32_t>(0x25DF30u + uint32_t(town) * 4);
        for (uint32_t i = 0; i < 400; ++i) {
            GeoAtraRow row;
            row.atra = elf.get<int32_t>(list + 12 * i);
            if (row.atra == -1) break;
            row.floor = elf.get<int32_t>(list + 12 * i + 4);
            row.count = elf.get<int32_t>(list + 12 * i + 8);
            t.atra[town].push_back(row);
        }
    }
    return t;
}

// ---- save data records ------------------------------------------------------------------------------------------------------------------------------

namespace {
constexpr size_t kInfo = 0x68, kInfoSize = 0x18;
}

uint8_t* Georama::info(int p) const {
    uint8_t* b = save_.grd_data(town_no_);
    return b && p >= 0 && p < 24 ? b + kInfo + size_t(p) * kInfoSize : nullptr;
}

Georama::Georama(Town& town, SaveData& save, const GeoTables& tables, int town_no) : town_(town), save_(save), tables_(tables), town_no_(std::clamp(town_no, 0, 5)) {}

static int16_t s16(const uint8_t* p) { int16_t v; std::memcpy(&v, p, 2); return v; }
static void put16(uint8_t* p, int v) { const int16_t x = int16_t(v); std::memcpy(p, &x, 2); }

bool Georama::obtained(int p) const { const uint8_t* i = info(p); return i && s16(i + 4) != 0; }

int Georama::max_copies(int p) const {
    const uint8_t* i = info(p);
    if (!i) return 0;
    const int saved = s16(i + 8);
    return saved > 0 ? saved : tables_.parts[town_no_][p].max;   // 0 = never changed: the table value
}

int Georama::placed(int p) const {
    int n = 0;
    int river = -1;
    for (const auto& kv : town_.info.parts) if (kv.second.directive == "RIVER_PARTS") { river = kv.first; break; }
    for (const EditSlot& s : town_.slots) {
        if (!s.used) continue;
        if (s.def == p) ++n;
        if (p == river && s.def != river) {
            auto it = town_.info.parts.find(s.def);
            if (it != town_.info.parts.end()) { const int k = edit_part_kind(it->second); if (k == 3 || k == 5) ++n; }
        }
    }
    return n;
}

bool Georama::installed(int p, int k) const { const uint8_t* i = info(p); return i && k >= 0 && k < 6 && s16(i + 10 + 2 * k) != 0; }
void Georama::set_installed(int p, int k, bool on) { uint8_t* i = info(p); if (i && k >= 0 && k < 6) put16(i + 10 + 2 * k, on ? 1 : 0); }

bool Georama::complete(int p) const {
    if (p < 0 || p >= 24) return false;
    const GeoPartDef& d = tables_.parts[town_no_][p];
    for (int k = 0; k < 6; ++k)
        if (d.elem[k].id >= 0 && !installed(p, k)) return false;
    return true;
}

bool Georama::comp_event(int p) const { const uint8_t* i = info(p); uint32_t f = 0; if (i) std::memcpy(&f, i, 4); return (f & 1) != 0; }
void Georama::set_comp_event(int p, bool on) {
    uint8_t* i = info(p);
    if (!i) return;
    uint32_t f;
    std::memcpy(&f, i, 4);
    f = on ? (f | 1u) : (f & ~1u);
    std::memcpy(i, &f, 4);
}

int Georama::part_count() const {
    int n = 0;
    while (n < 24 && tables_.parts[town_no_][n].max > 0) ++n;
    return n;
}

// ---- Atla pick-ups ---------------------------------------------------------------------------------------------------------------------------------

void georama_atra_get(SaveData& save, const GeoTables& tables, int town, int atra_id) {
    if (town < 0 || town > 5 || atra_id < 0) return;
    if (atra_id < 0x28) {   // AtraPartsGet
        const int p = atra_id;
        uint8_t* b = save.grd_data(town);
        if (!b || p >= 24) return;
        uint8_t* i = b + kInfo + size_t(p) * kInfoSize;
        const GeoPartDef& d = tables.parts[town][p];
        if (d.stackable < 1) {
            if (s16(i + 4) != 0) return;
            put16(i + 4, 1);
        } else {
            const int cur = s16(i + 4) == 0 ? 0 : s16(i + 8);
            put16(i + 8, s16(i + 4) == 0 ? 5 : std::min(cur + 5, d.max));
            put16(i + 4, 1);
        }
        if (s16(i + 8) <= 0) put16(i + 8, d.max);
    } else {   // AtraChipGet: into the first free slot of the tray
        uint8_t* tray = save.elem_data(town);
        if (!tray) return;
        const int chip = atra_id - 0x28;
        for (int s = 0; s < 0x80; ++s)
            if (s16(tray + 2 * s) == -1) { put16(tray + 2 * s, chip); return; }
    }
}

// ---- placing ---------------------------------------------------------------------------------------------------------------------------------------

int Georama::place(int def, int rot, const float cursor[3], std::string* why) {
    if (def < 0 || def > 23 || !usable(def)) { if (why) *why = "not available"; return -1; }
    if (placed(def) >= max_copies(def)) { if (why) *why = "no copies left"; return -1; }
    return edit_set_part(town_, def, rot, cursor, true, why);
}

int Georama::remove_at(const float cursor[3], int* rot_out) {
    const int slot = edit_slot_at(town_, cursor);
    if (slot < 0) return -1;
    const EditSlot s = town_.slots[size_t(slot)];
    const bool ok = edit_delete_slot(town_, slot);
    if (!ok) return -1;
    if (rot_out) *rot_out = s.rot;
    return s.def;
}

void Georama::save_layout() {
    std::vector<SaveData::GrdPart> list;
    int river = -1;
    for (const auto& kv : town_.info.parts) if (kv.second.directive == "RIVER_PARTS") { river = kv.first; break; }
    (void)river;
    for (const EditSlot& s : town_.slots) {
        if (!s.used || list.size() >= 0x82) continue;
        SaveData::GrdPart g;
        g.part = int16_t(s.def);
        g.rot = int16_t(s.rot);
        for (int k = 0; k < 3; ++k) g.pos[k] = s.pos[k];
        // roads and rivers are stored with their base part number (the shape follows from the neighbours)
        auto it = town_.info.parts.find(s.def);
        (void)it;
        list.push_back(g);
    }
    save_.set_town_parts(town_no_, list);
    // (the copy counts are recomputed on load; the request state is kept by request_check)
}

std::vector<PlacedPart> Georama::saved_layout(const SaveData& save, int town_no) {
    std::vector<PlacedPart> out;
    for (const SaveData::GrdPart& g : save.town_parts(town_no)) {
        PlacedPart pp;
        pp.part = g.part;
        pp.rot = g.rot;
        for (int k = 0; k < 3; ++k) pp.pos[k] = g.pos[k];
        out.push_back(pp);
    }
    return out;
}

// ---- requests --------------------------------------------------------------------------------------------------------------------------------------

namespace {

struct Rect { int x, z, w, h; };

struct Req {
    Town& t;
    const Georama& g;
    struct P {
        int slot = -1, type = -1, area = 0, cx = 0, cz = 0, rot = 0, w0 = 1, h0 = 1;
        int w() const { return (rot & 1) ? h0 : w0; }
        int h() const { return (rot & 1) ? w0 : h0; }
    };
    std::vector<P> parts;

    Req(Town& town, const Georama& geo) : t(town), g(geo) {
        for (size_t i = 0; i < t.slots.size(); ++i) {
            const EditSlot& s = t.slots[i];
            if (!s.used) continue;
            P p;
            p.slot = int(i); p.type = s.def; p.area = s.area; p.cx = s.cx; p.cz = s.cz; p.rot = s.rot;
            edit_footprint_size(t, s.def, p.w0, p.h0);
            parts.push_back(p);
        }
    }
    const P* L(int type) const {
        for (const P& p : parts) if (p.type == type) return &p;
        return nullptr;
    }
    bool area_ok(int a) const { return a >= 0 && size_t(a) < t.areas.size(); }
    int kind_at(int area, int x, int z) const {
        const Town::Area& a = t.areas[size_t(area)];
        if (x < 0 || z < 0 || x >= a.def.width || z >= a.def.height) return -1;
        return a.kind[size_t(z * a.def.width + x)];
    }
    int alt_at(const P& p) const { const Town::Area& a = t.areas[size_t(p.area)]; return a.alt[size_t(p.cz * a.def.width + p.cx)]; }
    Rect rect_part(const P& p, int m) const { return {p.cx - (p.w() >> 1) - m, p.cz - (p.h() >> 1) - m, p.w() + 2 * m, p.h() + 2 * m}; }
    Rect rect_cell(const P& p, int dx, int dz) const {
        const int rot = p.rot;
        const int W = p.w0, H = p.h0;
        dx -= W >> 1;
        dz -= H >> 1;
        int ox = 0, oz = 0;
        const int pw = W & 1, ph = H & 1;
        if (pw == 1 && ph == 0) { if (rot == -1) ox = -1; if (rot == 2) oz = -1; }
        if (pw == 0 && ph == 1) { if (rot == 1) oz = -1; if (rot == 2) ox = -1; }
        if (pw == 0 && ph == 0) { if (rot == -1) ox = -1; if (rot == 1) oz = -1; if (rot == 2) { ox = -1; oz = -1; } }
        int a, b;
        if (rot == -1) { a = -dz; b = dx; }
        else if (rot == 2) { a = -dx; b = -dz; }
        else if (rot == 1) { a = dz; b = -dx; }
        else { a = dx; b = dz; }
        return {ox + p.cx + a, oz + p.cz + b, 1, 1};
    }
    Rect rect_dir(const P& p, int d, int depth) const {
        const int w = p.w(), h = p.h();
        int u = d + p.rot;
        if (u > 2) u -= 4;
        if (u < -1) u += 4;
        const int cxo = w >> 1, czo = h >> 1;
        if (u == 0) return {p.cx - cxo, p.cz - czo - depth, w, depth};
        if (u == 2) return {p.cx - cxo, h + p.cz - czo, w, depth};
        if (u == 1) return {p.cx - cxo - depth, p.cz - czo, depth, h};
        return {w + p.cx - cxo, p.cz - czo, depth, h};
    }
    std::vector<int> slots_in(int area, const Rect& r) const {
        std::vector<int> out;
        if (!area_ok(area)) return out;
        const Town::Area& a = t.areas[size_t(area)];
        for (int x = r.x; x < r.x + r.w; ++x)
            for (int z = r.z; z < r.z + r.h; ++z) {
                if (x < 0 || z < 0 || x >= a.def.width || z >= a.def.height) continue;
                const int sl = a.slot[size_t(z * a.def.width + x)];
                if (sl >= 0 && std::find(out.begin(), out.end(), sl) == out.end()) out.push_back(sl);
            }
        return out;
    }
    bool has(const Rect& r, int type, int area = 0) const {
        for (int sl : slots_in(area, r)) if (t.slots[size_t(sl)].def == type) return true;
        return false;
    }
    int count_types(int area, const Rect& r, std::initializer_list<int> types, bool below7 = false) const {
        int n = 0;
        for (int sl : slots_in(area, r)) {
            const int ty = t.slots[size_t(sl)].def;
            if (below7) { if (ty < 7) ++n; }
            else if (std::find(types.begin(), types.end(), ty) != types.end()) ++n;
        }
        return n;
    }
    static bool check_rot(const P* a, const P* b, int k = 0) {
        if (!a || !b) return false;
        int r = a->rot + k;
        if (r > 2) r -= 4;
        if (r < -1) r += 4;
        return r == b->rot;
    }
    bool river_chain(int area, int x0, int z0, int x1, int z1) const {
        std::set<std::pair<int, int>> seen;
        std::function<bool(int, int)> rec = [&](int x, int z) -> bool {
            if (x < 0 || x >= 16 || z < 0 || z >= 16 || seen.count({x, z})) return false;
            seen.insert({x, z});
            const int k = kind_at(area, x, z);
            if (k != 2 && k != 3 && k != 5) return false;
            if (x == x1 && z == z1) return true;
            return rec(x - 1, z) || rec(x + 1, z) || rec(x, z - 1) || rec(x, z + 1);
        };
        return rec(x0, z0);
    }
};

}  // namespace

void Georama::request_check() {
    requests_.fill(false);
    Req q(town_, *this);
    const int area_count = int(town_.areas.size());
    auto has_areas = [&](int n) { return area_count >= n; };
    auto set = [&](int i) { if (i >= 0 && i < 24) requests_[size_t(i)] = true; };
    auto L = [&](int t) { return q.L(t); };
    const int n = town_no_;
    int total = 0;
    const int int_flag0 = save_.game_int(0);
    if (n == 0) {   // NornRequest
        total = 8;
        if (L(0) && L(0)->rot == 1) set(0);
        if (L(1) && q.has({10, 0, 4, 5}, 1, 0)) set(1);
        if (L(2) && !q.has({2, 8, 9, 4}, 2, 0)) set(2);
        if (L(3) && L(12) && q.has(q.rect_part(*L(12), 2), 3, 0)) set(3);
        if (L(4) && L(6) && q.has(q.rect_part(*L(6), 4), 4, 0)) set(4);
        if (L(5) && L(8) && q.has(q.rect_dir(*L(8), 2, 4), 5, 0) && int_flag0 >= 100) set(5);
        if (L(6) && L(1) && !q.has(q.rect_part(*L(1), 4), 6, 0)) set(6);
        if (L(7) && q.count_types(L(7)->area, q.rect_part(*L(7), 4), {}, true) >= 2) set(7);
    } else if (n == 1 && has_areas(3)) {   // MatatagiRequest
        total = 10;
        if (q.river_chain(0, 5, 0, 2, 7) && q.river_chain(1, 3, 5, 11, 3) && q.river_chain(2, 4, 0, 3, 7)) set(16);
        if (L(0) && (q.has({0, 0, 4, 1}, 0, 0) || q.has({0, 5, 1, 1}, 0, 1))) set(0);
        if (L(1) && q.count_types(L(1)->area, q.rect_part(*L(1), 1), {15}) > 3) set(1);
        if (L(2)) { const Rect r = q.rect_part(*L(2), 3); if (q.has(r, 11, L(2)->area) || q.has(r, 12, L(2)->area) || q.has(r, 13, L(2)->area)) set(2); }
        if (L(3) && q.has(q.rect_part(*L(3), 4), 14, L(3)->area)) set(3);
        if (L(4) && q.has({0, 5, 7, 3}, 4, 2)) set(4);
        if (L(5) && q.alt_at(*L(5)) > 0) set(5);
        if (L(6) && L(3) && q.has(q.rect_part(*L(3), 3), 6, L(3)->area)) set(6);
        if (L(7) && q.alt_at(*L(7)) > 0) set(7);
        if (L(14) && q.count_types(L(14)->area, q.rect_part(*L(14), 1), {16, 17, 11, 12, 13}) > 13) set(14);
    } else if (n == 2 && has_areas(3)) {   // QueensRequest
        total = 10;
        if (L(0) && (q.has({0, 0, 1, 7}, 0, 0) || q.has({0, 0, 1, 8}, 0, 2))) set(0);
        if (L(1) && L(10) && q.has(q.rect_part(*L(10), 3), 1, L(10)->area)) set(1);
        if (L(2) && L(8) && q.has(q.rect_part(*L(8), 2), 2, L(8)->area)) set(2);
        if (L(3) && L(1) && q.has(q.rect_part(*L(1), 2), 3, L(1)->area)) set(3);
        if (L(4) && (!L(9) || L(4)->area != L(9)->area)) set(4);
        if (L(5) && L(5)->rot == 0) set(5);
        if (L(6) && L(11) && L(6)->rot == -1 && q.has(q.rect_part(*L(11), 3), 6, L(11)->area)) set(6);
        if (L(7) && L(7)->area == 0) set(7);
        if (L(8) && q.has(q.rect_cell(*L(8), 0, 3), 13, L(8)->area)) set(8);
        if (L(9) && L(8) && q.has(q.rect_part(*L(8), 1), 9, L(8)->area)) set(9);
    } else if (n == 3 && has_areas(1)) {   // MuskaRequest
        total = 9;
        auto front = [&](const Req::P* p, int ty) { return q.has(q.rect_dir(*p, 2, 14), ty, 0); };
        if (L(0) && L(8) && q.has({1, 0, 6, 3}, 0, 0) && front(L(0), 8) && Req::check_rot(L(0), L(8), 1) && comp_event(0)) set(0);
        if (L(1) && L(10) && front(L(1), 10) && Req::check_rot(L(1), L(10), 1)) set(1);
        if (L(2) && L(9) && front(L(2), 9) && Req::check_rot(L(2), L(9), 0)) set(2);
        if (L(3) && L(9) && L(11) && q.has(q.rect_part(*L(11), 2), 3, 0) && front(L(3), 9) && Req::check_rot(L(3), L(9), 2)) set(3);
        if (L(4) && L(9) && front(L(4), 9) && Req::check_rot(L(4), L(9), 1)) set(4);
        if (L(5) && L(10) && front(L(5), 10) && Req::check_rot(L(5), L(10), 0)) set(5);
        if (L(6) && L(8) && front(L(6), 8) && Req::check_rot(L(6), L(8), 0) && L(6)->rot == 1) set(6);
        if (L(7) && L(8) && front(L(7), 8) && Req::check_rot(L(7), L(8), 2)) set(7);
        if (L(10) && q.has({2, 0, 3, 14}, 10, 0) && L(10)->rot == 0) set(10);
    } else if (n == 4 && has_areas(1)) {   // YellowRequest
        total = 13;
        auto cr = [&](int a, int b) { return Req::check_rot(L(a), L(b), 0); };
        auto cell = [&](int part, int dx, int dz, int ty) { return L(part) && q.has(q.rect_cell(*L(part), dx, dz), ty, 0); };
        if (cr(0, 7) && cell(0, 0, 2, 7) && cell(0, 1, 2, 7)) set(0);
        if (cr(1, 5) && cell(1, 0, -1, 5)) set(1);
        if (cr(2, 6) && cell(2, 0, -1, 6)) set(2);
        if (cr(3, 7) && cell(3, 2, 1, 7) && !cell(3, 2, 0, 7)) set(3);
        if (cr(4, 7) && cell(4, -1, 1, 7) && !cell(4, -1, 0, 7)) set(4);
        if (cr(5, 3) && cell(3, 1, 2, 5)) set(5);
        if (cr(6, 4) && cell(4, 0, 2, 6)) set(6);
        if (L(7)) set(7);
        if (cr(8, 7) && cell(8, 0, -1, 7) && cell(8, 1, -1, 7)) set(8);
        if (cr(9, 8) && cell(8, 0, 1, 9)) set(9);
        if (cr(10, 8) && cell(8, 1, 1, 10)) set(10);
        if (cr(11, 9) && cell(11, 0, -1, 9)) set(11);
        if (cr(12, 10) && cell(12, 0, -1, 10)) set(12);
    }
    request_total_ = total;
    for (int i = 0; i < 24; ++i)   // a wish only counts when that house is complete
        if (!complete(i)) requests_[size_t(i)] = false;
    if (uint8_t* b = save_.grd_data(town_no_)) {   // RequestCheck keeps them in GrdData (saved; the houses' talk scripts read them)
        const uint32_t t32 = uint32_t(total);
        std::memcpy(b, &t32, 4);
        for (int i = 0; i < 24; ++i) { const uint32_t v = requests_[size_t(i)] ? 1u : 0u; std::memcpy(b + 4 + 4 * i, &v, 4); }
    }
}

static uint32_t grd_u32(const SaveData& save, int town, size_t off) {
    const uint8_t* b = save.grd_data(town);
    uint32_t v = 0;
    if (b) std::memcpy(&v, b + off, 4);
    return v;
}

bool Georama::request(int p) const { return p >= 0 && p < 24 && grd_u32(save_, town_no_, 4 + 4 * size_t(p)) != 0; }
int Georama::request_total() const { return int(grd_u32(save_, town_no_, 0)); }

int Georama::request_count() const {
    int n = 0;
    for (int i = 0; i < 24; ++i) if (request(i)) ++n;
    return n;
}

int Georama::request_percent() const {
    const int total = request_total();
    if (total < 1) return 100;
    const int n = request_count();
    return n >= total ? 100 : n * 100 / total;
}

bool Georama::request_event_flag() const { return grd_u32(save_, town_no_, 0x64) != 0; }
void Georama::set_request_event_flag(bool on) {
    if (uint8_t* b = save_.grd_data(town_no_)) { const uint32_t v = on ? 1u : 0u; std::memcpy(b + 0x64, &v, 4); }
}

int georama_atra_parts_count(const SaveData& save, int town) {
    const uint8_t* b = save.grd_data(town);
    int n = 0;
    if (!b) return 0;
    for (int p = 0; p < 24; ++p) {
        int16_t v;
        std::memcpy(&v, b + 0x68 + size_t(p) * 0x18 + 4, 2);
        if (v != 0) ++n;
    }
    return n;
}

// ---- villagers -------------------------------------------------------------------------------------------------------------------------------------

int Georama::villager_part(int v) const {
    if (town_no_ > 4 || v < 0 || v >= 20) return -1;
    return tables_.chara[town_no_][v].part;
}

bool Georama::villager_present(int v) const {
    if (town_no_ > 4 || v < 0 || v >= 20) return true;
    const GeoCharaRow& r = tables_.chara[town_no_][v];
    if (r.part < 0) return true;
    bool standing = false;
    for (const EditSlot& s : town_.slots) if (s.used && s.def == r.part) { standing = true; break; }
    if (!standing || !obtained(r.part)) return false;
    if (r.element >= 0 && !installed(r.part, r.element)) return false;
    if (complete(r.part) && !comp_event(r.part) && r.hide_until != 0) return false;
    return true;
}

// ---- the Atla board --------------------------------------------------------------------------------------------------------------------------------

bool Georama::tip_can_install(int p, int k, int chip) const {
    if (p < 0 || p >= 24 || k < 0 || k >= 6 || !usable(p)) return false;
    const GeoElemDef& e = tables_.parts[town_no_][p].elem[k];
    if (e.id < 0 || installed(p, k) || chip != e.id) return false;
    if (e.rel >= 0 && !installed(p, e.rel)) return false;
    if (e.id < 0x28 && e.npc >= 0) {   // the villager must have been asked about the house
        const uint8_t* b = save_.grd_data(town_no_);
        uint32_t flags = 0;
        if (b) std::memcpy(&flags, b + 0xac8 + 0xc * size_t(e.npc), 4);
        if (!(flags & 2)) return false;
    }
    return true;
}

bool Georama::install_tip(int p, int k, int chip) {
    if (!tip_can_install(p, k, chip) || complete(p)) return false;
    set_installed(p, k, true);
    return true;
}

int Georama::tray_get(int slot) const {
    const uint8_t* tray = save_.elem_data(town_no_);
    return tray && slot >= 0 && slot < 0x80 ? s16(tray + 2 * slot) : -1;
}

void Georama::tray_set(int slot, int chip) {
    uint8_t* tray = save_.elem_data(town_no_);
    if (tray && slot >= 0 && slot < 0x80) put16(tray + 2 * slot, chip);
}

std::vector<std::pair<std::string, bool>> Georama::element_frames(int p) const {
    std::vector<std::pair<std::string, bool>> out;
    if (p < 0 || p >= 24) return out;
    const GeoPartDef& d = tables_.parts[town_no_][p];
    for (int k = 0; k < 6; ++k) {
        const GeoElemDef& e = d.elem[k];
        if (e.id < 0 || e.id >= 0x24) continue;
        const bool on = installed(p, k);
        const std::string& list = e.names[0];
        size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && list[i] == ' ') ++i;
            size_t j = i;
            while (j < list.size() && list[j] != ' ') ++j;
            if (j > i) {
                std::string name = list.substr(i, j - i);
                bool invert = false;
                if (name[0] == '-') { invert = true; name.erase(0, 1); }
                else if (name[0] == '*') name.erase(0, 1);
                if (!name.empty()) out.push_back({name, invert ? !on : on});
            }
            i = j;
        }
    }
    return out;
}

int Georama::pending_completion_under(const float hero[3]) const {
    const int slot = edit_slot_at(town_, hero);
    if (slot < 0) return -1;
    const int p = town_.slots[size_t(slot)].def;
    if (p < 0 || p >= 24) return -1;
    if (tables_.parts[town_no_][p].comp_kind == 2 && !comp_event(p) && complete(p)) return p;
    return -1;
}

}  // namespace dccore
