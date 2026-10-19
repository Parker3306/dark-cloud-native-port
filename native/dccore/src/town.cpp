#include "dccore/town.h"
#include "dccore/edit.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include "dccore/pack.h"
#include "dccore/tim2.h"

namespace dccore {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string base_name(const std::string& s) {
    const size_t k = s.find_last_of("/\\");
    return k == std::string::npos ? s : s.substr(k + 1);
}

std::string strip_ext(const std::string& s) {
    const size_t k = s.find_last_of('.');
    return k == std::string::npos ? s : s.substr(0, k);
}

// local -> world of an instance: rotation about +Y by `yaw` (x' = x cos + z sin, z' = -x sin + z cos), then the position
void to_world(const TownInstance& in, const float p[3], float o[3]) {
    const float c = std::cos(in.yaw), s = std::sin(in.yaw);
    o[0] = p[0] * c + p[2] * s + in.pos[0];
    o[1] = p[1] + in.pos[1];
    o[2] = -p[0] * s + p[2] * c + in.pos[2];
}

void rotate_dir(const TownInstance& in, const float p[3], float o[3]) {
    const float c = std::cos(in.yaw), s = std::sin(in.yaw);
    o[0] = p[0] * c + p[2] * s;
    o[1] = p[1];
    o[2] = -p[0] * s + p[2] * c;
}

TownAsset asset_from_pts(const Pts& p, std::vector<std::string>& failed) {
    TownAsset a;
    a.name = p.name;
    a.is_pts = true;
    a.info = p.info;
    for (int s = 0; s < kPtsSlots; ++s) {
        if (!p.has(s)) continue;
        try {
            if (s == kPtsCollision) {
                a.col = parse_collision_mds(p.model(s));
                a.has_col = true;
            } else if (s == kPtsCamera) {
                a.cam = parse_collision_mds(p.model(s));
                a.has_cam = true;
            } else if (s <= kPtsLod3) {
                a.lod[s] = build_static_model(parse_mds(p.model(s), false));
            } else if (s == kPtsWater) {
                a.water = build_static_model(parse_mds(p.model(s), false));
            }
        } catch (const std::exception&) {
            failed.push_back(p.name + "#" + std::to_string(s));
        }
    }
    return a;
}

void bounds_of(const TownAsset& a, float lo[3], float hi[3]) {
    lo[0] = lo[1] = lo[2] = 1e30f;
    hi[0] = hi[1] = hi[2] = -1e30f;
    auto add = [&](const float* v) {
        for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
    };
    if (a.has_col && !a.col.polys.empty()) {
        for (const CCPoly& q : a.col.polys) for (int v = 0; v < 3; ++v) add(q.v[v]);
    } else {
        for (const Surface& s : a.lod[0].surfaces)
            for (size_t i = 0; i + 2 < s.positions.size(); i += 3) add(&s.positions[i]);
    }
    if (lo[0] > hi[0]) { for (int k = 0; k < 3; ++k) lo[k] = hi[k] = 0; }
}

// the footprint cell of the rotated part: g(i, j) with the rotated size (w, h)
uint8_t footprint_cell(const PtsInfo& f, int rot, int i, int j) {
    const int W0 = f.width, H0 = f.height;
    size_t idx = 0;
    switch (rot) {
        case 0: idx = size_t(i + j * W0); break;
        case 1: idx = size_t(i * W0 + (W0 - 1 - j)); break;
        case 2: idx = size_t(W0 * (H0 - 1 - j) + (W0 - 1 - i)); break;
        default: idx = size_t(j + W0 * (H0 - 1 - i)); break;
    }
    return idx < f.grid.size() ? f.grid[idx] : 0;
}

}  // namespace

// Trigger points (doors, items, event zones, ladders) and villager standing points of one instance: from the cfg and the function records of its model.
void build_instance_triggers(Town& t, size_t ii) {
        const TownInstance& in = t.instances[ii];
        const TownAsset& a = t.assets[size_t(in.asset)];
        const MapOwned* owner = nullptr;
        if (in.owner_object >= 0) owner = &t.info.objects[size_t(in.owner_object)];
        else if (in.part_def >= 0) owner = &t.info.parts.at(in.part_def);
        // static doors given by the cfg itself (MAPJUMP + ENTRANCE)
        if (owner)
            for (int evi : owner->events) {
                const MapEvent& me = t.info.events[size_t(evi - 1)];
                if (!me.has_door) continue;
                bool from_record = false;   // a part with its own door record gets the trigger from that record
                for (const PtsFunc& fn : a.info.funcs)
                    if (fn.type == kFnMapJump && fn.id == me.jump_id) from_record = true;
                if (from_record) continue;
                TownEvent e;
                e.kind = TownEvent::MapJump;
                e.map = me.map;
                e.jump_id = me.jump_id;
                e.door_side = me.side;
                to_world(in, me.door_pos, e.pos);   // the door position is given relative to the object
                e.yaw = in.yaw + me.door_rot[1];
                e.extent = std::max(me.extent[0], me.extent[2]);
                e.instance = int(ii);
                t.events.push_back(e);
            }
        for (const PtsFunc& fn : a.info.funcs) {
            float wp[3];
            to_world(in, fn.pos, wp);
            const float yaw = in.yaw + fn.rot[1];
            switch (fn.type) {
                case kFnMapJump: {
                    TownEvent e;
                    e.kind = TownEvent::MapJump;
                    e.jump_id = fn.id;
                    if (owner)
                        for (int evi : owner->events)
                            if (t.info.events[size_t(evi - 1)].jump_id == fn.id) { e.map = t.info.events[size_t(evi - 1)].map; break; }
                    for (int k = 0; k < 3; ++k) e.pos[k] = wp[k];
                    e.yaw = yaw;
                    e.extent = 10;
                    e.flag = fn.flag;
                    e.t_start = fn.t_start;
                    e.t_end = fn.t_end;
                    e.event_no = int(fn.params[0]);
                    e.instance = int(ii);
                    t.events.push_back(e);
                    break;
                }
                case kFnMapJumpCamp:
                    for (TownEvent& e : t.events)
                        if (e.instance == int(ii) && e.kind == TownEvent::MapJump && e.jump_id == fn.id) {
                            for (int k = 0; k < 3; ++k) e.out_pos[k] = wp[k];
                            e.has_out = true;
                        }
                    break;
                case kFnDoorR: case kFnDoorL:
                    for (TownEvent& e : t.events)
                        if (e.instance == int(ii) && e.kind == TownEvent::MapJump && e.jump_id == fn.id) {
                            e.door_side = fn.type == kFnDoorR ? 1 : -1;
                            e.door_style = int(fn.params[0]);
                        }
                    break;
                case kFnItem: {
                    TownEvent e;
                    e.kind = TownEvent::Item;
                    for (int k = 0; k < 3; ++k) e.pos[k] = wp[k];
                    e.extent = 15;
                    e.item = int(fn.params[0]);
                    e.count = int(fn.params[1]);
                    e.min_progress = int(fn.params[2]);
                    e.flag = fn.flag;
                    e.initially_active = fn.enabled != 0;
                    e.instance = int(ii);
                    t.events.push_back(e);
                    break;
                }
                case kFnEvent:
                    if (int(fn.params[0]) > 0) {
                        TownEvent e;
                        e.kind = TownEvent::Event;
                        for (int k = 0; k < 3; ++k) e.pos[k] = wp[k];
                        e.extent = std::max(fn.scale[0], fn.scale[2]);
                        e.event_no = int(fn.params[0]);
                        e.flag = fn.flag;
                        e.t_start = fn.t_start;
                        e.t_end = fn.t_end;
                        e.instance = int(ii);
                        t.events.push_back(e);
                    }
                    break;
                case kFnLadderBottom: case kFnLadderTop: {
                    TownEvent e;
                    e.kind = fn.type == kFnLadderBottom ? TownEvent::LadderBottom : TownEvent::LadderTop;
                    e.jump_id = fn.id;
                    for (int k = 0; k < 3; ++k) e.pos[k] = wp[k];
                    e.yaw = yaw;
                    e.extent = 6;
                    e.instance = int(ii);
                    t.events.push_back(e);
                    break;
                }
                case kFnPerson: {
                    TownPerson p;
                    p.villager = fn.id;
                    for (int k = 0; k < 3; ++k) p.pos[k] = wp[k];
                    p.yaw = yaw;
                    p.instance = int(ii);
                    t.people.push_back(p);
                    break;
                }
                default: break;
            }
        }
}

void remove_instance_triggers(Town& t, size_t ii) {
    t.events.erase(std::remove_if(t.events.begin(), t.events.end(), [&](const TownEvent& e) { return e.instance == int(ii); }), t.events.end());
    t.people.erase(std::remove_if(t.people.begin(), t.people.end(), [&](const TownPerson& p) { return p.instance == int(ii); }), t.people.end());
}

int town_asset(Town& t, const std::string& file) {
    const std::string key = strip_ext(base_name(file));
    auto it = t.asset_by_key.find(key);
    if (it != t.asset_by_key.end()) return it->second;
    int idx = -1;
    if (const Pts* p = t.scn.find(file)) {
        t.assets.push_back(asset_from_pts(*p, t.failed));
        idx = int(t.assets.size()) - 1;
    }
    t.asset_by_key[key] = idx;
    return idx;
}

std::vector<PlacedPart> parse_gdata_edt(View data) {
    std::vector<PlacedPart> out;
    if (data.size() < 8) return out;
    const int n = rd<int16_t>(data, 0), off = rd<int16_t>(data, 4);
    for (int i = 0; i < n; ++i) {
        const size_t p = size_t(off) + size_t(i) * 16;
        if (p + 16 > data.size()) break;
        PlacedPart pp;
        pp.part = rd<int16_t>(data, p);
        pp.rot = rd<int16_t>(data, p + 2);
        if (pp.part < 0) break;
        for (int k = 0; k < 3; ++k) pp.pos[k] = rd<float>(data, p + 4 + 4 * size_t(k));
        out.push_back(pp);
    }
    return out;
}

Town load_town(const Disc& disc, const std::string& name, const std::vector<PlacedPart>& layout) {
    Town t;
    t.name = name;
    t.dir = "gedit/" + name + "/";
    t.layout = layout;
    {
        const Bytes cfg = disc.read(t.dir + "mapinfo.cfg");
        t.info = parse_mapinfo(cfg);
    }
    if (disc.contains(t.dir + t.info.scn)) t.scn = parse_scn(disc.read(t.dir + t.info.scn));
    for (const PeopleDef& p : t.info.people) t.villager_names.push_back(p.name);

    // textures of the map: every container named in the cfg lives in img.pak
    std::unique_ptr<Pack> img_pak;
    if (disc.contains(t.dir + "img.pak")) img_pak = std::make_unique<Pack>(disc.read(t.dir + "img.pak"));
    std::unique_ptr<Pack> mds_pak;
    if (disc.contains(t.dir + "mds.pak")) mds_pak = std::make_unique<Pack>(disc.read(t.dir + "mds.pak"));
    if (img_pak)
        for (const TextureRef& tr : t.info.textures) {
            const PackEntry* e = img_pak->find(base_name(tr.file));
            if (!e) continue;
            try {
                View iv = img_pak->data(*e);
                for (const ImgTexture& it : img_textures(iv)) {
                    t.textures[it.name] = Tim2(iv, it.offset).decode(false, it.swizzled);
                    if (tr.type == 0x15) t.water_textures.push_back(it.name);
                }
            } catch (const std::exception&) {
                t.failed.push_back(tr.file);
            }
        }

    std::map<std::string, int>& asset_of = t.asset_by_key;   // part file stem -> asset index
    auto asset_for_name = [&](const std::string& file) -> int {
        const std::string key = strip_ext(base_name(file));
        auto it = asset_of.find(key);
        if (it != asset_of.end()) return it->second;
        int idx = -1;
        if (const Pts* p = t.scn.find(file)) {
            t.assets.push_back(asset_from_pts(*p, t.failed));
            idx = int(t.assets.size()) - 1;
        } else if (mds_pak) {
            // not a part of the scene: a model file of the pack (<name>.mds or <name>_0.mds)
            for (const std::string& cand : {key + ".mds", key + "_0.mds"}) {
                if (const PackEntry* e = mds_pak->find(cand)) {
                    TownAsset a;
                    a.name = key;
                    try {
                        a.lod[0] = build_static_model(parse_mds(mds_pak->data(*e), false));
                    } catch (const std::exception&) {
                        t.failed.push_back(cand);
                    }
                    t.assets.push_back(std::move(a));
                    idx = int(t.assets.size()) - 1;
                    break;
                }
            }
        }
        asset_of[key] = idx;
        return idx;
    };

    if (mds_pak) {
        for (auto* set : {&t.info.skies, &t.info.suns}) {
            for (const auto& [n, def] : *set) {
                const PackEntry* e = mds_pak->find(base_name(def.file));
                if (!e) continue;
                try {
                    (set == &t.info.skies ? t.skies : t.suns)[n] = build_static_model(parse_mds(mds_pak->data(*e), false));
                } catch (const std::exception&) {
                    t.failed.push_back(def.file);
                }
            }
        }
    }

    constexpr float kDeg = 3.14159265358979f / 180.0f;
    auto finish_instance = [&](TownInstance& in) {
        const TownAsset& a = t.assets[size_t(in.asset)];
        float lo[3], hi[3];
        bounds_of(a, lo, hi);
        float wl[3] = {1e30f, 1e30f, 1e30f}, wh[3] = {-1e30f, -1e30f, -1e30f};
        for (int c = 0; c < 8; ++c) {
            const float corner[3] = {c & 1 ? hi[0] : lo[0], c & 2 ? hi[1] : lo[1], c & 4 ? hi[2] : lo[2]};
            float w[3];
            to_world(in, corner, w);
            for (int k = 0; k < 3; ++k) { wl[k] = std::min(wl[k], w[k]); wh[k] = std::max(wh[k], w[k]); }
        }
        for (int k = 0; k < 3; ++k) { in.bbox_min[k] = wl[k]; in.bbox_max[k] = wh[k]; }
    };

    // static objects
    for (size_t oi = 0; oi < t.info.objects.size(); ++oi) {
        const MapObject& o = t.info.objects[oi];
        const int ai = asset_for_name(o.names[0]);
        if (ai < 0) continue;
        TownInstance in;
        in.asset = ai;
        in.owner_object = int(oi);
        for (int k = 0; k < 3; ++k) in.pos[k] = o.pos[k];
        in.yaw = o.rot_deg[1] * kDeg;
        finish_instance(in);
        t.instances.push_back(in);
    }

    // edit areas
    for (const auto& [n, def] : t.info.edit_areas) {
        Town::Area a;
        a.def = def;
        a.part.assign(size_t(def.width) * size_t(def.height), -1);
        a.code.assign(a.part.size(), 0);
        if (mds_pak)
            if (const PackEntry* ge = mds_pak->find(base_name(def.grid))) {
                try {
                    a.grid = build_static_model(parse_mds(mds_pak->data(*ge), false));
                } catch (const std::exception&) {
                    t.failed.push_back(def.grid);
                }
            }
        t.areas.push_back(std::move(a));
    }
    std::vector<int> area_ids;
    for (const auto& kv : t.info.edit_areas) area_ids.push_back(kv.first);

    // placed parts: the editor's own placement (no checks); bridges and on-river parts come last because they replace river pieces
    for (Town::Area& A : t.areas) {
        A.kind.assign(A.part.size(), 0);
        A.alt.assign(A.part.size(), 0);
        A.slot.assign(A.part.size(), -1);
    }
    for (int pass = 0; pass < 2; ++pass)
        for (const PlacedPart& pp : layout) {
            auto pit = t.info.parts.find(pp.part);
            if (pit == t.info.parts.end()) continue;
            const int kind = edit_part_kind(pit->second);
            if ((kind == 3 || kind == 5) != (pass == 1)) continue;
            edit_set_part(t, pp.part, pp.rot, pp.pos, false);
        }
    edit_reshape_all(t);
    t.edit_log.clear();

    // trigger points and standing points of the function records
    for (size_t ii = 0; ii < t.instances.size(); ++ii)
        if (t.instances[ii].part_def < 0) build_instance_triggers(t, ii);   // (placed parts got theirs when the editor put them down)
    return t;
}

namespace {

void transform_piece(const CollisionPiece& piece, const TownInstance& in, std::vector<CCPoly>& out) {
    const float c = std::cos(in.yaw), s = std::sin(in.yaw);
    for (const CCPoly& q : piece.polys) {
        CCPoly r = q;
        for (int v = 0; v < 3; ++v) {
            const float x = q.v[v][0], z = q.v[v][2];
            r.v[v][0] = x * c + z * s + in.pos[0];
            r.v[v][1] = q.v[v][1] + in.pos[1];
            r.v[v][2] = -x * s + z * c + in.pos[2];
        }
        const float nx = q.n[0], nz = q.n[2];
        r.n[0] = nx * c + nz * s;
        r.n[2] = -nx * s + nz * c;
        out.push_back(r);
    }
}

// two triangles per edit-area cell near (x, z), 1% larger than the cell, at the height of the area's origin (CEditArea::PickUpPoly)
void flat_cell_polys(const Town& t, float x, float z, float reach, std::vector<CCPoly>& out) {
    for (const Town::Area& A : t.areas) {
        const EditAreaDef& d = A.def;
        const int x0 = std::max(0, int(std::floor((x - reach - d.origin[0]) / d.unit))), x1 = std::min(d.width - 1, int(std::floor((x + reach - d.origin[0]) / d.unit)));
        const int z0 = std::max(0, int(std::floor((z - reach - d.origin[2]) / d.unit))), z1 = std::min(d.height - 1, int(std::floor((z + reach - d.origin[2]) / d.unit)));
        for (int gz = z0; gz <= z1; ++gz)
            for (int gx = x0; gx <= x1; ++gx) {
                if (A.code[size_t(gz * d.width + gx)] == 0x81) continue;
                const float pad = d.unit * 0.005f;
                const float ax = d.origin[0] + float(gx) * d.unit - pad, bx = d.origin[0] + float(gx + 1) * d.unit + pad;
                const float az = d.origin[2] + float(gz) * d.unit - pad, bz = d.origin[2] + float(gz + 1) * d.unit + pad;
                const float y = d.origin[1];
                CCPoly p{};
                p.n[1] = 1.0f;
                const float tri[2][3][2] = {{{ax, az}, {ax, bz}, {bx, az}}, {{bx, az}, {ax, bz}, {bx, bz}}};
                for (int k = 0; k < 2; ++k) {
                    for (int v = 0; v < 3; ++v) { p.v[v][0] = tri[k][v][0]; p.v[v][1] = y; p.v[v][2] = tri[k][v][1]; p.v[v][3] = 1.0f; }
                    out.push_back(p);
                }
            }
    }
}

}  // namespace

std::vector<CCPoly> town_polys_near(const Town& t, float x, float y, float z) {
    std::vector<CCPoly> out;
    constexpr float kReach = 30.0f;
    for (const TownInstance& in : t.instances) {
        if (!in.visible) continue;
        const TownAsset& a = t.assets[size_t(in.asset)];
        if (!a.has_col) continue;
        if (x + kReach < in.bbox_min[0] || x - kReach > in.bbox_max[0] || z + kReach < in.bbox_min[2] || z - kReach > in.bbox_max[2]) continue;
        if (y + 100 < in.bbox_min[1] || y - 100 > in.bbox_max[1]) continue;
        transform_piece(a.col, in, out);
    }
    flat_cell_polys(t, x, z, kReach, out);
    return out;
}

std::vector<CCPoly> town_camera_polys(const Town& t, float x, float y, float z) {
    (void)y;
    std::vector<CCPoly> out;
    constexpr float kReach = 120.0f;
    for (const TownInstance& in : t.instances) {
        if (!in.visible) continue;
        const TownAsset& a = t.assets[size_t(in.asset)];
        if (!a.has_cam) continue;
        if (x + kReach < in.bbox_min[0] || x - kReach > in.bbox_max[0] || z + kReach < in.bbox_min[2] || z - kReach > in.bbox_max[2]) continue;
        transform_piece(a.cam, in, out);
    }
    flat_cell_polys(t, x, z, kReach, out);
    return out;
}

}  // namespace dccore

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// house interiors

namespace dccore {

namespace {

using M4 = std::array<double, 16>;

M4 mul4(const M4& a, const M4& b) {   // row vectors: a is applied first
    M4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a[size_t(i * 4 + k)] * b[size_t(k * 4 + j)];
            r[size_t(i * 4 + j)] = s;
        }
    return r;
}

M4 inverse4(const M4& m) {
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) { a[i][j] = m[size_t(i * 4 + j)]; a[i][4 + j] = i == j ? 1.0 : 0.0; }
    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r) if (std::fabs(a[r][c]) > std::fabs(a[piv][c])) piv = r;
        for (int j = 0; j < 8; ++j) std::swap(a[c][j], a[piv][j]);
        const double d = a[c][c] != 0 ? a[c][c] : 1.0;
        for (int j = 0; j < 8; ++j) a[c][j] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    M4 out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[size_t(i * 4 + j)] = a[i][4 + j];
    return out;
}

}  // namespace

Town load_interior(const Disc& disc, const std::string& town, const std::string& house, int variant) {
    Town t;
    t.interior = true;
    t.parent = town;
    t.name = house;
    t.dir = "gedit/" + town + "/in/" + house + "/";
    static const char* kSuffix[3] = {"m", "e", "n"};
    const std::string stem = house + kSuffix[std::clamp(variant, 0, 2)];
    // the town villager list: interior people are numbered like the PEOPLE2 entries of the town
    std::vector<PeopleDef> villagers;
    if (disc.contains("gedit/" + town + "/mapinfo.cfg")) villagers = parse_mapinfo(disc.read("gedit/" + town + "/mapinfo.cfg")).people;
    for (const PeopleDef& p : villagers) t.villager_names.push_back(p.name);
    t.info = parse_mapinfo(disc.read(t.dir + stem + ".cfg"), true);
    for (PeopleDef& p : t.info.people)
        if (p.part >= 0 && size_t(p.part) < villagers.size()) p.name = villagers[size_t(p.part)].name;
    t.scn = parse_scn(disc.read(t.dir + stem + ".scn"));
    if (disc.contains(t.dir + stem + ".img")) {
        const Bytes img = disc.read(t.dir + stem + ".img");
        for (const ImgTexture& it : img_textures(img)) t.textures[it.name] = Tim2(img, it.offset).decode(false, it.swizzled);
    }
    if (t.scn.parts.empty()) return t;
    const Pts& pts = t.scn.parts[0];
    t.assets.push_back(asset_from_pts(pts, t.failed));
    TownInstance in;
    in.asset = 0;
    float lo[3], hi[3];
    bounds_of(t.assets[0], lo, hi);
    for (int k = 0; k < 3; ++k) { in.bbox_min[k] = lo[k]; in.bbox_max[k] = hi[k]; }
    t.instances.push_back(in);

    // the camera zones hang on named frames of the room
    std::vector<std::string> names;
    std::vector<std::array<double, 16>> world;
    try {
        const Mds mds = parse_mds(pts.model(0), false);
        world = world_matrices(mds);
        for (const Object& o : mds.objects) names.push_back(o.name);
    } catch (const std::exception&) {
    }
    for (const PtsFunc& fn : pts.info.funcs) {
        switch (fn.type) {
            case kFnMapJump: {
                TownEvent e;
                e.kind = TownEvent::MapJump;
                e.jump_id = fn.id;
                for (int k = 0; k < 3; ++k) e.pos[k] = fn.pos[k];
                e.yaw = fn.rot[1];
                e.extent = 10;
                e.flag = fn.flag;
                e.t_start = fn.t_start;
                e.t_end = fn.t_end;
                e.event_no = int(fn.params[0]);
                t.events.push_back(e);
                break;
            }
            case kFnDoorR: case kFnDoorL:
                for (TownEvent& e : t.events)
                    if (e.kind == TownEvent::MapJump && e.jump_id == fn.id) { e.door_side = fn.type == kFnDoorR ? 1 : -1; e.door_style = int(fn.params[0]); }
                break;
            case kFnEvent:
                if (int(fn.params[0]) > 0) {
                    TownEvent e;
                    e.kind = TownEvent::Event;
                    for (int k = 0; k < 3; ++k) e.pos[k] = fn.pos[k];
                    e.extent = std::max(fn.scale[0], fn.scale[2]);
                    e.event_no = int(fn.params[0]);
                    e.flag = fn.flag;
                    t.events.push_back(e);
                }
                break;
            case kFnItem: {
                TownEvent e;
                e.kind = TownEvent::Item;
                for (int k = 0; k < 3; ++k) e.pos[k] = fn.pos[k];
                e.extent = 15;
                e.item = int(fn.params[0]);
                e.count = int(fn.params[1]);
                e.flag = fn.flag;
                t.events.push_back(e);
                break;
            }
            case kFnCameraZone: {
                CameraZone z;
                z.id = fn.id;
                for (int k = 0; k < 3; ++k) { z.box_min[k] = fn.scale[k]; z.box_max[k] = fn.params[k]; }
                M4 lw{};
                for (int k = 0; k < 16; ++k) lw[size_t(k)] = (k % 5 == 0) ? 1.0 : 0.0;
                const std::string key = frame_key(fn.target);
                for (size_t i = 0; i < names.size(); ++i)
                    if (frame_key(names[i]) == key) { lw = world[i]; break; }
                M4 rec;
                for (int k = 0; k < 16; ++k) rec[size_t(k)] = fn.matrix[k];
                const M4 w = mul4(rec, lw);   // the record matrix first, then the frame
                z.cam_pos[0] = w[12]; z.cam_pos[1] = w[13]; z.cam_pos[2] = w[14];
                const M4 inv = inverse4(w);
                for (int k = 0; k < 16; ++k) z.to_local[k] = inv[size_t(k)];
                if (t.cam_zones.size() < 8) t.cam_zones.push_back(z);
                break;
            }
            case kFnPerson: {
                TownPerson p;
                p.villager = fn.id;
                for (int k = 0; k < 3; ++k) p.pos[k] = fn.pos[k];
                p.yaw = fn.rot[1];
                t.people.push_back(p);
                break;
            }
            default: break;
        }
    }
    // the villagers named by the config of the house
    for (const PeopleDef& d : t.info.people) {
        TownPerson p;
        p.villager = d.part;
        for (int k = 0; k < 3; ++k) p.pos[k] = d.pos[k];
        p.yaw = d.rot_y;
        t.people.push_back(p);
    }
    return t;
}

bool InteriorCamera::update(const Town& t, const float hero[3], float cam_pos[3]) {
    if (t.cam_zones.empty()) return false;
    int chosen = -1, fallback = -1;
    for (size_t i = 0; i < t.cam_zones.size(); ++i) {
        const CameraZone& z = t.cam_zones[i];
        if (z.id == 0) { fallback = int(i); continue; }
        const double* m = z.to_local;
        const double x = hero[0], y = hero[1], zz = hero[2];
        const double lx = x * m[0] + y * m[4] + zz * m[8] + m[12], ly = x * m[1] + y * m[5] + zz * m[9] + m[13], lz = x * m[2] + y * m[6] + zz * m[10] + m[14];
        if (z.box_min[0] <= lx && z.box_min[1] <= ly && z.box_min[2] <= lz && lx <= z.box_max[0] && ly <= z.box_max[1] && lz <= z.box_max[2]) { chosen = int(i); break; }
    }
    if (chosen < 0) chosen = fallback;
    if (chosen < 0) return false;
    if (active < 0) active = chosen;
    if (counter < 1) { counter = 0; active = chosen; }
    if (chosen == active) counter = 20; else --counter;
    const CameraZone& z = t.cam_zones[size_t(active)];
    cam_pos[0] = float(z.cam_pos[0]); cam_pos[1] = float(z.cam_pos[1]); cam_pos[2] = float(z.cam_pos[2]);
    return true;
}

}  // namespace dccore
