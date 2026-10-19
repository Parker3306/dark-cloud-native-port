#include "dccore/edit.h"
#include <algorithm>
#include <cmath>

namespace dccore {

namespace {

constexpr float kPi = 3.14159265358979f;

struct Footprint {
    int w = 0, h = 0;
    std::vector<uint8_t> g;
};

const PartsDef* part_def(const Town& t, int def) {
    auto it = t.info.parts.find(def);
    return it == t.info.parts.end() ? nullptr : &it->second;
}

Footprint footprint_of(Town& t, int def) {
    Footprint f;
    const PartsDef* d = part_def(t, def);
    if (!d) return f;
    if (!d->file.empty()) {
        const int ai = town_asset(t, d->file);
        if (ai >= 0 && t.assets[size_t(ai)].is_pts) {
            const PtsInfo& pi = t.assets[size_t(ai)].info;
            f.w = pi.width; f.h = pi.height;
            f.g.assign(pi.grid.begin(), pi.grid.end());
            return f;
        }
    }
    if (d->has_info) {
        f.w = d->info.width; f.h = d->info.height;
        for (char c : d->info.grid) {
            if (c == '|') continue;
            f.g.push_back(c == 'x' ? 0x80 : c == 'd' ? 0x81 : (c >= '0' && c <= '9') ? uint8_t(c - '0') : 0);
        }
    }
    return f;
}

// the footprint cell (i, j) of the rotated part (the cases of CMapParts::GetInfoData)
uint8_t cell_g(const Footprint& f, int rot, int i, int j) {
    const int W0 = f.w, H0 = f.h;
    size_t idx = 0;
    switch (rot) {
        case 0: idx = size_t(i + j * W0); break;
        case 1: idx = size_t(i * W0 + (W0 - 1 - j)); break;
        case 2: idx = size_t(W0 * (H0 - 1 - j) + (W0 - 1 - i)); break;
        default: idx = size_t(j + W0 * (H0 - 1 - i)); break;
    }
    return idx < f.g.size() ? f.g[idx] : 0;
}

void rotated_size(const Footprint& f, int rot, int& w, int& h) {
    const bool odd = (rot & 1) != 0;
    w = odd ? f.h : f.w;
    h = odd ? f.w : f.h;
}

size_t cell_index(const Town::Area& a, int cx, int cz) { return size_t(cz * a.def.width + cx); }
bool in_area(const Town::Area& a, int cx, int cz) { return cx >= 0 && cz >= 0 && cx < a.def.width && cz < a.def.height; }

// GetPartsExtra: the kind of a cell; Matataki pretends a river in six cells outside its areas
int extra_of(const Town& t, size_t area, int cx, int cz) {
    if (t.name == "e02" && area < 3) {
        static const int fixed[3][2][2] = {{{5, -1}, {2, 8}}, {{3, 6}, {12, 3}}, {{4, -1}, {3, 8}}};
        for (const auto& c : fixed[area]) if (c[0] == cx && c[1] == cz) return 2;
    }
    const Town::Area& a = t.areas[area];
    if (!in_area(a, cx, cz)) return -1;
    return a.kind[cell_index(a, cx, cz)];
}

int first_def_of(const Town& t, const char* directive) {
    for (const auto& kv : t.info.parts)
        if (kv.second.directive == directive) return kv.first;
    return -1;
}

// SetRoadParts / SetRiverParts: shape (1 corner, 2 straight, 3 T, 4 cross, 5 single, 6 dead end) and rotation index
bool road_river_shape(const Town& t, size_t area, int cx, int cz, int kind, int& shape, int& rot) {
    auto nb = [&](int dx, int dz) {
        const int e = extra_of(t, area, cx + dx, cz + dz);
        return kind == 1 ? (e == 1 ? 1 : 0) : ((e == 2 || e == 3 || e == 5) ? 1 : 0);
    };
    const int n[4] = {nb(0, -1), nb(1, 0), nb(0, 1), nb(-1, 0)};
    const int count = n[0] + n[1] + n[2] + n[3];
    shape = -1;
    int r = 0;
    if (count == 4) shape = 4;
    if (count == 3) {
        shape = 3;
        if (kind == 1) { if (!n[1]) r = 3; if (!n[2]) r = 2; if (!n[3]) r = 1; }
        else { if (!n[0]) r = 3; if (!n[1]) r = 2; if (!n[2]) r = 1; if (!n[3]) r = 0; }
    }
    if (count == 2) {
        if ((!n[0] || !n[2]) && (!n[1] || !n[3])) {
            shape = 1;
            if (kind == 1) { if (n[1] && n[2]) r = 0; if (n[2] && n[3]) r = 3; if (n[3] && n[0]) r = 2; if (n[0] && n[1]) r = 1; }
            else { if (n[1] && n[2]) r = 3; if (n[2] && n[3]) r = 2; if (n[3] && n[0]) r = 1; if (n[0] && n[1]) r = 0; }
        } else {
            shape = 2;
            if (n[0]) r = 0;
            if (n[1]) r = 1;
        }
    }
    if (count == 1) { shape = 6; if (n[0]) r = 2; if (n[1]) r = 1; if (n[2]) r = 0; if (n[3]) r = 3; }
    if (count == 0) shape = 5;
    rot = r > 2 ? -1 : r;
    return shape >= 1;
}

void finish_bounds(Town& t, TownInstance& in) {
    const TownAsset& a = t.assets[size_t(in.asset)];
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    auto add = [&](const float* v) { for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); } };
    if (a.has_col && !a.col.polys.empty()) { for (const CCPoly& q : a.col.polys) for (int v = 0; v < 3; ++v) add(q.v[v]); }
    else for (const Surface& s : a.lod[0].surfaces) for (size_t i = 0; i + 2 < s.positions.size(); i += 3) add(&s.positions[i]);
    if (lo[0] > hi[0]) { for (int k = 0; k < 3; ++k) lo[k] = hi[k] = 0; }
    float wl[3] = {1e30f, 1e30f, 1e30f}, wh[3] = {-1e30f, -1e30f, -1e30f};
    const float c = std::cos(in.yaw), s = std::sin(in.yaw);
    for (int k = 0; k < 8; ++k) {
        const float p[3] = {k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2]};
        const float w[3] = {p[0] * c + p[2] * s + in.pos[0], p[1] + in.pos[1], -p[0] * s + p[2] * c + in.pos[2]};
        for (int j = 0; j < 3; ++j) { wl[j] = std::min(wl[j], w[j]); wh[j] = std::max(wh[j], w[j]); }
    }
    for (int j = 0; j < 3; ++j) { in.bbox_min[j] = wl[j]; in.bbox_max[j] = wh[j]; }
}

// make (or replace) the instance of a slot with the given asset / yaw / position
void set_slot_instance(Town& t, EditSlot& s, int asset, float yaw, bool log) {
    if (asset < 0) return;
    if (s.instance >= 0 && size_t(s.instance) < t.instances.size() && !t.instances[size_t(s.instance)].removed) {
        TownInstance& in = t.instances[size_t(s.instance)];
        remove_instance_triggers(t, size_t(s.instance));
        in.asset = asset;
        in.yaw = yaw;
        for (int k = 0; k < 3; ++k) in.pos[k] = s.pos[k];
        in.rot = s.rot;
        finish_bounds(t, in);
        build_instance_triggers(t, size_t(s.instance));
        if (log) t.edit_log.push_back({EditChange::Update, s.instance});
        return;
    }
    TownInstance in;
    in.asset = asset;
    in.part_def = s.def;
    in.rot = s.rot;
    in.area = s.area;
    in.yaw = yaw;
    for (int k = 0; k < 3; ++k) in.pos[k] = s.pos[k];
    finish_bounds(t, in);
    t.instances.push_back(in);
    s.instance = int(t.instances.size()) - 1;
    build_instance_triggers(t, size_t(s.instance));
    if (log) t.edit_log.push_back({EditChange::Add, s.instance});
}

void remove_slot_instance(Town& t, EditSlot& s, bool log) {
    if (s.instance < 0 || size_t(s.instance) >= t.instances.size()) return;
    TownInstance& in = t.instances[size_t(s.instance)];
    remove_instance_triggers(t, size_t(s.instance));
    in.removed = true;
    in.visible = false;
    if (log) t.edit_log.push_back({EditChange::Remove, s.instance});
    s.instance = -1;
}

// the asset a road / river cell shows for its shape
int variant_asset(Town& t, int kind, int shape) {
    if (kind == 1) { auto it = t.info.roads.find(shape - 1); return it == t.info.roads.end() ? -1 : town_asset(t, std::get<0>(it->second)); }
    auto it = t.info.rivers.find(shape - 1);
    return it == t.info.rivers.end() ? -1 : town_asset(t, std::get<0>(it->second));
}

void reshape_cell(Town& t, size_t area, int cx, int cz, bool log) {
    Town::Area& a = t.areas[area];
    if (!in_area(a, cx, cz)) return;
    const size_t ci = cell_index(a, cx, cz);
    const int kind = a.kind[ci];
    if (kind != 1 && kind != 2) return;
    const int sl = a.slot[ci];
    if (sl < 0 || !t.slots[size_t(sl)].used) return;
    int shape, rot;
    if (!road_river_shape(t, area, cx, cz, kind, shape, rot)) return;
    EditSlot& s = t.slots[size_t(sl)];
    s.variant = shape - 1;
    set_slot_instance(t, s, variant_asset(t, kind, shape), kPi * float(rot) * 0.5f, log);
}

void reshape_around(Town& t, size_t area, int cx, int cz, bool log) {
    reshape_cell(t, area, cx, cz, log);
    reshape_cell(t, area, cx, cz - 1, log);
    reshape_cell(t, area, cx + 1, cz, log);
    reshape_cell(t, area, cx, cz + 1, log);
    reshape_cell(t, area, cx - 1, cz, log);
}

void clear_cells(Town& t, EditSlot& s) {
    if (s.area < 0) return;
    Town::Area& a = t.areas[size_t(s.area)];
    const int slot = int(&s - t.slots.data());
    for (size_t i = 0; i < a.slot.size(); ++i) {
        if (a.slot[i] != slot) continue;
        a.slot[i] = -1;
        a.part[i] = -1;
        a.code[i] = 0;
        a.kind[i] = 0;
    }
    // raised cells (digits of the footprint) are given back
}

void mark_cells(Town& t, EditSlot& s, const Footprint& fp, int slot) {
    Town::Area& a = t.areas[size_t(s.area)];
    const PartsDef* d = part_def(t, s.def);
    int w, h;
    rotated_size(fp, s.rot, w, h);
    const int kind = d ? edit_part_kind(*d) : 0;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const uint8_t g = cell_g(fp, s.rot, i, j);
            const int gx = s.cx + i - (w >> 1), gz = s.cz + j - (h >> 1);
            if (g == 0 || !in_area(a, gx, gz)) continue;
            const size_t ci = cell_index(a, gx, gz);
            if (g >= 0x80) {
                a.part[ci] = s.def;
                a.slot[ci] = int16_t(slot);
                a.code[ci] = g;
                a.kind[ci] = uint8_t(kind);
            } else {
                a.alt[ci] = int8_t(a.alt[ci] + int(g));
            }
        }
}

void unmark_alt(Town& t, EditSlot& s, const Footprint& fp) {
    Town::Area& a = t.areas[size_t(s.area)];
    int w, h;
    rotated_size(fp, s.rot, w, h);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const uint8_t g = cell_g(fp, s.rot, i, j);
            const int gx = s.cx + i - (w >> 1), gz = s.cz + j - (h >> 1);
            if (g == 0 || g >= 0x80 || !in_area(a, gx, gz)) continue;
            const size_t ci = cell_index(a, gx, gz);
            a.alt[ci] = int8_t(a.alt[ci] - int(g));
        }
}

}  // namespace

int edit_part_kind(const PartsDef& def) {
    const std::string& d = def.directive;
    if (d == "ROAD_PARTS") return 1;
    if (d == "RIVER_PARTS") return 2;
    if (d == "BRIDGE_PARTS") return 3;
    if (d == "LAKE_PARTS") return 4;
    if (d == "ON_RIVER_PARTS") return 5;
    return 0;
}

int edit_find_area(const Town& t, const float pos[3]) {
    for (size_t k = 0; k < t.areas.size(); ++k) {
        const EditAreaDef& d = t.areas[k].def;
        const float fx = (pos[0] - (d.origin[0] - 0.01f * float(d.width)) + 0.1f) / d.unit, fz = (pos[2] - (d.origin[2] - 0.01f * float(d.height)) + 0.1f) / d.unit;
        if (fx >= 0 && fz >= 0 && fx < float(d.width) && fz < float(d.height)) return int(k);
    }
    return -1;
}

void edit_cell(const Town::Area& a, const float pos[3], int& cx, int& cz) {
    cx = int(std::floor((pos[0] - (a.def.origin[0] - 0.01f * float(a.def.width)) + 0.1f) / a.def.unit));
    cz = int(std::floor((pos[2] - (a.def.origin[2] - 0.01f * float(a.def.height)) + 0.1f) / a.def.unit));
}

std::array<float, 3> edit_cell_center(const Town::Area& a, int cx, int cz) {
    const float ox = a.def.origin[0] - 0.01f * float(a.def.width), oz = a.def.origin[2] - 0.01f * float(a.def.height);
    float alt = 0;
    if (in_area(a, cx, cz)) alt = float(a.alt[cell_index(a, cx, cz)]);
    return {ox + (float(cx) + 0.5f) * a.def.unit, a.def.origin[1] + alt * a.def.unit_alt, oz + (float(cz) + 0.5f) * a.def.unit};
}

std::vector<std::array<int, 3>> edit_footprint_cells(Town& t, int def, int rot) {
    std::vector<std::array<int, 3>> out;
    const Footprint fp = footprint_of(t, def);
    int w, h;
    rotated_size(fp, rot, w, h);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) out.push_back({i - (w >> 1), j - (h >> 1), int(cell_g(fp, rot, i, j))});
    return out;
}

std::array<float, 3> edit_part_position(const Town& t, int area, int cx, int cz, int def, int rot) {
    const Town::Area& a = t.areas[size_t(area)];
    Footprint fp = footprint_of(const_cast<Town&>(t), def);
    int w, h;
    rotated_size(fp, rot, w, h);
    const float ox = a.def.origin[0] - 0.01f * float(a.def.width), oz = a.def.origin[2] - 0.01f * float(a.def.height);
    const float alt = in_area(a, cx, cz) ? float(a.alt[cell_index(a, cx, cz)]) : 0.0f;
    return {ox + float(cx) * a.def.unit + ((w & 1) ? a.def.unit * 0.5f : 0.0f), a.def.origin[1] + alt * a.def.unit_alt, oz + float(cz) * a.def.unit + ((h & 1) ? a.def.unit * 0.5f : 0.0f)};
}

bool edit_footprint_size(Town& t, int def, int& w, int& h) {
    const Footprint f = footprint_of(t, def);
    w = f.w;
    h = f.h;
    return f.w > 0 && f.h > 0;
}

bool edit_protected_cell(const Town& t, int area, int cx, int cz) {
    if (t.name != "e02") return false;
    static const int cells[3][2][2] = {{{5, 0}, {2, 7}}, {{3, 5}, {11, 3}}, {{4, 0}, {3, 7}}};
    if (area < 0 || area > 2) return false;
    for (const auto& c : cells[area]) if (c[0] == cx && c[1] == cz) return true;
    return false;
}

bool edit_check_parts(const Town& tc, int def, int rot, int area, int cx, int cz, std::string* why) {
    Town& t = const_cast<Town&>(tc);
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    const PartsDef* d = part_def(t, def);
    if (!d || area < 0 || size_t(area) >= t.areas.size()) return fail("no such part or area");
    const Town::Area& a = t.areas[size_t(area)];
    const Footprint fp = footprint_of(t, def);
    if (fp.w <= 0 || fp.h <= 0) return fail("no footprint");
    int w, h;
    rotated_size(fp, rot, w, h);
    const int x0 = cx - (w >> 1), z0 = cz - (h >> 1);
    if (x0 < 0 || z0 < 0 || x0 + w > a.def.width || z0 + h > a.def.height) return fail("outside the area");
    const int kind = edit_part_kind(*d);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            if (cell_g(fp, rot, i, j) == 0) continue;
            const size_t ci = cell_index(a, x0 + i, z0 + j);
            if (a.part[ci] >= 0) {
                const bool old_road = a.kind[ci] == 1;
                if (!(kind != 1 && old_road)) return fail("occupied");
            }
        }
    const int alt_c = a.alt[cell_index(a, cx, cz)];
    if (kind == 2) {
        if (alt_c > 0) return fail("raised ground");
        const int dx[4] = {0, 1, 0, -1}, dz[4] = {-1, 0, 1, 0};
        for (int k = 0; k < 4; ++k) {
            const int e = extra_of(t, size_t(area), cx + dx[k], cz + dz[k]);
            if (e == 3 || e == 4 || e == 5) return fail("next to a bridge or lake");
        }
    }
    if (kind == 4) {
        if (alt_c > 0) return fail("raised ground");
        for (int j = z0 - 1; j <= z0 + h; ++j)
            for (int i = x0 - 1; i <= x0 + w; ++i) {
                const int e = extra_of(t, size_t(area), i, j);
                if (e == 2 || e == 3 || e == 5) return fail("next to a river");
            }
    }
    return true;
}

int edit_slot_at(const Town& t, const float pos[3]) {
    const int area = edit_find_area(t, pos);
    if (area < 0) return -1;
    const Town::Area& a = t.areas[size_t(area)];
    int cx, cz;
    edit_cell(a, pos, cx, cz);
    return in_area(a, cx, cz) ? int(a.slot[cell_index(a, cx, cz)]) : -1;
}

int edit_set_part(Town& t, int def, int rot, const float pos[3], bool check, std::string* why) {
    const PartsDef* d = part_def(t, def);
    if (!d) return -1;
    // a saved footprint centre is looked up with a margin of one unit (Load: SetMapParts(x + 1.0, y, z + 1.0, ...)) against rounding on cell borders
    const float lookup[3] = {pos[0] + (check ? 0.0f : 1.0f), pos[1], pos[2] + (check ? 0.0f : 1.0f)};
    const int area = edit_find_area(t, lookup);
    if (area < 0) { if (why) *why = "outside every area"; return -1; }
    Town::Area& a = t.areas[size_t(area)];
    int cx, cz;
    edit_cell(a, lookup, cx, cz);
    if (!in_area(a, cx, cz)) return -1;
    const int kind = edit_part_kind(*d);
    const size_t cell = cell_index(a, cx, cz);
    Footprint fp = footprint_of(t, def);
    EditSlot s;
    s.used = true;
    s.def = def;
    s.rot = rot;
    s.area = area;
    s.cx = cx;
    s.cz = cz;

    if (kind == 3 || kind == 5) {   // a bridge or a part on the river: replaces the river piece under it
        int under = a.slot[cell];
        if (check) {
            if (under < 0 || edit_part_kind(*part_def(t, t.slots[size_t(under)].def)) != 2) { if (why) *why = "needs a river piece"; return -1; }
            if (t.slots[size_t(under)].variant != 1) { if (why) *why = "needs a straight river piece"; return -1; }
            if (edit_protected_cell(t, area, cx, cz)) { if (why) *why = "protected"; return -1; }
        } else if (under < 0 || edit_part_kind(*part_def(t, t.slots[size_t(under)].def)) != 2) {   // loading: the river piece comes first
            const int river = first_def_of(t, "RIVER_PARTS");
            if (river >= 0) under = edit_set_part(t, river, 0, pos, false);
        }
        if (under < 0) return -1;
        EditSlot& r = t.slots[size_t(under)];
        s.rot = check ? r.rot : rot;
        if (check) {   // the piece's own turn
            const float yaw = t.instances[size_t(r.instance)].yaw;
            s.rot = int(std::lround(yaw / (kPi * 0.5f)));
            if (s.rot > 2) s.rot = -1;
        }
        for (int k = 0; k < 3; ++k) s.pos[k] = r.pos[k];
        s.instance = r.instance;
        // keep the cells: the slot of the river piece now belongs to the new part
        s.variant = -1;
        const int sl = under;
        r = s;
        for (size_t i = 0; i < a.slot.size(); ++i) if (a.slot[i] == sl) { a.part[i] = def; a.kind[i] = uint8_t(kind); }
        int asset = -1;
        if (!d->file.empty()) asset = town_asset(t, d->file);
        else { auto it = t.info.rivers.find(6); if (it != t.info.rivers.end()) asset = town_asset(t, std::get<0>(it->second)); }
        set_slot_instance(t, r, asset, kPi * float(r.rot) * 0.5f, check);
        reshape_around(t, size_t(area), cx, cz, check);
        return under;
    }

    if (check && !edit_check_parts(t, def, rot, area, cx, cz, why)) return -1;
    int slot = -1;
    for (size_t i = 0; i < t.slots.size(); ++i) if (!t.slots[i].used) { slot = int(i); break; }
    if (slot < 0) return -1;
    if (check) {   // the cursor position decides the footprint centre
        const auto p = edit_part_position(t, area, cx, cz, def, rot);
        for (int k = 0; k < 3; ++k) s.pos[k] = p[size_t(k)];
    } else {
        for (int k = 0; k < 3; ++k) s.pos[k] = pos[k];
    }
    // a part other than a road standing on road pieces takes them away
    {
        int w, h;
        rotated_size(fp, rot, w, h);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                if (cell_g(fp, rot, i, j) == 0) continue;
                const int gx = cx - (w >> 1) + i, gz = cz - (h >> 1) + j;
                if (!in_area(a, gx, gz)) continue;
                const size_t ci = cell_index(a, gx, gz);
                if (kind != 1 && a.kind[ci] == 1 && a.slot[ci] >= 0) edit_delete_slot(t, a.slot[ci]);
            }
    }
    t.slots[size_t(slot)] = s;
    EditSlot& sref = t.slots[size_t(slot)];
    mark_cells(t, sref, fp, slot);
    if (kind == 1 || kind == 2) {
        sref.variant = -1;
        reshape_around(t, size_t(area), cx, cz, check);
        if (sref.instance < 0) {   // (the cell did not get a variant: no asset for its shape)
            const int ai = d->file.empty() ? -1 : town_asset(t, d->file);
            if (ai >= 0) set_slot_instance(t, sref, ai, kPi * float(rot) * 0.5f, check);
        }
    } else {
        const int ai = d->file.empty() ? -1 : town_asset(t, d->file);
        set_slot_instance(t, sref, ai, kPi * float(rot) * 0.5f, check);
    }
    return slot;
}

bool edit_delete_slot(Town& t, int slot) {
    if (slot < 0 || size_t(slot) >= t.slots.size() || !t.slots[size_t(slot)].used) return false;
    EditSlot& s = t.slots[size_t(slot)];
    const PartsDef* d = part_def(t, s.def);
    if (!d) return false;
    const int kind = edit_part_kind(*d);
    if (edit_protected_cell(t, s.area, s.cx, s.cz) && kind != 3 && kind != 5) return false;
    Town::Area& a = t.areas[size_t(s.area)];
    if (kind == 3 || kind == 5) {   // back to a plain river piece
        const int river = first_def_of(t, "RIVER_PARTS");
        if (river < 0) return false;
        s.def = river;
        for (size_t i = 0; i < a.slot.size(); ++i) if (a.slot[i] == slot) { a.part[i] = river; a.kind[i] = 2; }
        s.variant = -1;
        reshape_around(t, size_t(s.area), s.cx, s.cz, true);
        return true;
    }
    const Footprint fp = footprint_of(t, s.def);
    unmark_alt(t, s, fp);
    clear_cells(t, s);
    remove_slot_instance(t, s, true);
    const int area = s.area, cx = s.cx, cz = s.cz;
    s = EditSlot();
    if (kind == 1 || kind == 2) reshape_around(t, size_t(area), cx, cz, true);
    return true;
}

void edit_reshape_all(Town& t) {
    for (size_t k = 0; k < t.areas.size(); ++k) {
        Town::Area& a = t.areas[k];
        for (int cz = 0; cz < a.def.height; ++cz)
            for (int cx = 0; cx < a.def.width; ++cx) reshape_cell(t, k, cx, cz, false);
    }
}

}  // namespace dccore
