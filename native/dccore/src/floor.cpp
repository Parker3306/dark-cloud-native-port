#include "dccore/floor.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace dccore {

Part build_part_public(const Object& o, int index, const std::array<double, 16>& world);

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Splits "KEYWORD arg, arg, ..." into the keyword and its comma/space separated arguments; quoted strings stay together (without quotes).
void split_line(const std::string& line, std::string& key, std::vector<std::string>& args) {
    key.clear();
    args.clear();
    size_t p = 0;
    while (p < line.size() && !std::isspace(static_cast<unsigned char>(line[p]))) key += line[p++];
    std::string cur;
    bool in_quote = false, have = false;
    for (; p < line.size(); ++p) {
        char c = line[p];
        if (in_quote) {
            if (c == '"') in_quote = false;
            else cur += c;
            continue;
        }
        if (c == '"') { in_quote = true; have = true; continue; }
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            if (have) { args.push_back(cur); cur.clear(); have = false; }
            continue;
        }
        cur += c;
        have = true;
    }
    if (have) args.push_back(cur);
}

float num(const std::vector<std::string>& a, size_t i, float def = 0) { return i < a.size() ? float(std::atof(a[i].c_str())) : def; }

}  // namespace

DungeonCfg parse_dungeon_cfg(View text) {
    DungeonCfg cfg;
    std::string all(reinterpret_cast<const char*>(text.data()), text.size());
    std::istringstream in(all);
    std::string line, key;
    std::vector<std::string> a;
    PartDef* cur = nullptr;
    while (std::getline(in, line)) {
        size_t c = line.find("//");
        if (c != std::string::npos) line.resize(c);
        line = trim(line);
        if (line.empty()) continue;
        split_line(line, key, a);
        if (key == "DEF_PATS") { cfg.parts.emplace_back(); cur = &cfg.parts.back(); continue; }
        if (key == "DEF_ENDS") { cur = nullptr; continue; }
        if (cur) {
            if (key == "PT_BASE" && !a.empty()) {
                PartModelRef m;
                m.file = a[0];
                m.slot = int(num(a, 1));
                for (size_t k = 0; k < 3; ++k) m.offset[k] = num(a, 2 + k);
                m.rot = int(num(a, 5));
                cur->models.push_back(m);
            } else if (key == "PT_COLS" && !a.empty()) {
                cur->collisions.push_back({a[0], int(num(a, 1))});
            } else if (key == "PT_CAM" && !a.empty()) {
                cur->cameras.push_back({a[0], int(num(a, 1))});
            } else if (key == "PT_HIT_MARKER" && a.size() >= 5) {
                PartMarker m;
                m.name = a[0];
                m.radius = num(a, 1);
                m.event_id = int(num(a, 2));
                m.hit = true;
                m.chara = int(num(a, 3));
                m.attack = int(num(a, 4));
                auto on = [&](size_t i) { return i < a.size() && a[i] == "ON"; };
                m.sw = on(5); m.extra = on(6);
                cur->markers.push_back(m);
            } else if (key == "PT_MARKER" && a.size() >= 3) {
                PartMarker m;
                m.name = a[0];
                m.radius = num(a, 1);
                m.event_id = int(num(a, 2));
                auto on = [&](size_t i) { return i < a.size() && a[i] == "ON"; };
                m.use_item = on(3); m.link = on(4); m.sw = on(5); m.extra = on(6);
                cur->markers.push_back(m);
            } else if (key == "PT_FIRE") {
                cur->fires.push_back({num(a, 0), num(a, 1), num(a, 2)});
            }
            continue;
        }
        const bool ura = key.rfind("URA_", 0) == 0;
        CfgEnv& env = ura ? cfg.back : cfg.front;
        const std::string k = ura ? key.substr(4) : key;
        if (k == "SET_PATH" && !a.empty()) {
            cfg.path = a[0];
        } else if (k == "AMBIENT") {
            for (size_t i = 0; i < 3; ++i) env.ambient[i] = num(a, i);
        } else if (k == "LIGHT_C") {
            CfgLight l;
            for (size_t i = 0; i < 3; ++i) { l.dir[i] = num(a, i); l.color[i] = num(a, 3 + i); }
            l.index = int(num(a, 6));
            env.lights.push_back(l);
        } else if (k == "FOG") {
            env.fog_near = num(a, 0);
            env.fog_far = num(a, 1);
            for (size_t i = 0; i < 3; ++i) env.fog_color[i] = num(a, 2 + i);
        } else if (k == "BG_COL") {
            for (size_t i = 0; i < 3; ++i) env.bg[i] = num(a, i);
        } else if (k == "VIEWLEVEL") {
            cfg.view_level = num(a, 0);
        } else if (k == "GRD_IMG" && !a.empty()) {
            cfg.ground_img = a[0];
        } else if (k == "DRANS_PARTS" && !a.empty()) {
            cfg.drans_parts.push_back(a[0]);
        } else if (k == "DRANS_COLS" && !a.empty()) {
            cfg.drans_cols.push_back(a[0]);
        } else if (k == "MINIMAP_IMG" && !a.empty()) {
            cfg.minimap_img = a[0];
        }
    }
    return cfg;
}

StaticModel build_static_model(const Mds& mds) {
    StaticModel out;
    const auto world = world_matrices(mds);
    std::vector<uint8_t> is_door(mds.objects.size(), 0);
    std::vector<std::string> paths(mds.objects.size());
    for (size_t i = 0; i < mds.objects.size(); ++i) {
        const int parent = mds.objects[i].parent;
        paths[i] = (parent >= 0 && size_t(parent) < i ? paths[size_t(parent)] + "|" : std::string()) + mds.objects[i].name;
    }
    for (size_t i = 0; i < mds.objects.size(); ++i) {
        const int parent = mds.objects[i].parent;
        is_door[i] = uint8_t(door_kind_of(mds.objects[i].name));
        if (!is_door[i] && parent >= 0 && size_t(parent) < i) is_door[i] = is_door[size_t(parent)];
    }
    for (size_t i = 0; i < mds.objects.size(); ++i) {
        if (!mds.objects[i].has_mesh) continue;
        Part part = build_part_public(mds.objects[i], int(i), world[i]);
        const auto& m = world[i];
        for (Surface& s : part.surfaces) {
            if (s.indices.empty()) continue;
            for (size_t v = 0; v + 2 < s.positions.size(); v += 3) {
                const double x = s.positions[v], y = s.positions[v + 1], z = s.positions[v + 2];
                s.positions[v] = float(x * m[0] + y * m[4] + z * m[8] + m[12]);
                s.positions[v + 1] = float(x * m[1] + y * m[5] + z * m[9] + m[13]);
                s.positions[v + 2] = float(x * m[2] + y * m[6] + z * m[10] + m[14]);
                const double nx = s.normals[v], ny = s.normals[v + 1], nz = s.normals[v + 2];
                double tx = nx * m[0] + ny * m[4] + nz * m[8], ty = nx * m[1] + ny * m[5] + nz * m[9], tz = nx * m[2] + ny * m[6] + nz * m[10];
                const double l = std::sqrt(tx * tx + ty * ty + tz * tz);
                if (l > 1e-12) { tx /= l; ty /= l; tz /= l; }
                s.normals[v] = float(tx);
                s.normals[v + 1] = float(ty);
                s.normals[v + 2] = float(tz);
            }
            if (lower(mds.objects[i].name).rfind("effect", 0) == 0) s.additive = true;
            s.object = mds.objects[i].name;
            s.path = paths[i];
            s.pivot[0] = float(world[i][12]); s.pivot[1] = float(world[i][13]); s.pivot[2] = float(world[i][14]);
            if (is_door[i]) out.door_surfaces[is_door[i]].push_back(std::move(s));
            else out.surfaces.push_back(std::move(s));
        }
    }
    return out;
}

int model_turn_index(int cell_rot, int model_rot) {
    int i = cell_rot + model_rot;
    if (i > 3) i -= 3;
    if (i == 3) i = -1;
    return i;
}

Dungeon load_dungeon(const std::string& cfg_text, const Pack& mpd) {
    Dungeon d;
    d.cfg = parse_dungeon_cfg(View(reinterpret_cast<const uint8_t*>(cfg_text.data()), cfg_text.size()));
    for (const auto& e : mpd.entries()) {
        const std::string n = lower(e.name);
        if (n.size() <= 4 || n.compare(n.size() - 4, 4, ".mds") != 0) continue;
        // collision (_a) meshes have their own layout; camera (_v) meshes are not read yet
        const bool is_col = n.size() > 6 && n.compare(n.size() - 6, 2, "_a") == 0;
        const bool is_cam = n.size() > 6 && n.compare(n.size() - 6, 2, "_v") == 0;
        if (is_cam) {
            try {
                d.cam_meshes[e.name] = parse_collision_mds(mpd.data(e));
            } catch (const std::exception&) {
                d.failed_models.push_back(e.name);
            }
            continue;
        }
        if (is_col) {
            try {
                d.collisions[e.name] = parse_collision_mds(mpd.data(e));
            } catch (const std::exception&) {
                d.failed_models.push_back(e.name);
            }
            continue;
        }
        try {
            const Mds mds = parse_mds(mpd.data(e), false);
            d.models[e.name] = build_static_model(mds);
            const auto world = world_matrices(mds);
            for (size_t i = 0; i < mds.objects.size(); ++i)
                d.marker_frames[e.name][mds.objects[i].name] = {float(world[i][12]), float(world[i][13]), float(world[i][14])};
        } catch (const std::exception&) {
            d.failed_models.push_back(e.name);
        }
    }
    for (const std::string& name : d.cfg.drans_parts)   // the arena tiles
        if (const PackEntry* e = mpd.find(name)) {
            try {
                View v = mpd.data(*e);
                d.drans.push_back(std::make_shared<Character>(load_character(Pack(Bytes(v.begin(), v.end())))));
            } catch (const std::exception&) {
                d.failed_models.push_back(name);
            }
        }
    if (!d.cfg.ground_img.empty())
        if (const PackEntry* img = mpd.find(d.cfg.ground_img)) {
            View iv = mpd.data(*img);
            for (const auto& t : img_textures(iv)) d.textures[t.name] = Tim2(iv, t.offset).decode(false, t.swizzled);
        }
    return d;
}

}  // namespace dccore

namespace dccore {

std::vector<CCPoly> floor_polys_near(const Dungeon& d, const FloorLayout& layout, float x, float z, int radius) {
    std::vector<CCPoly> out;
    if (layout.fixed) {
        const float origin[3] = {0, 0, 0};
        for (const PartDef& part : d.cfg.parts)
            for (const PartRef& c : part.collisions) {
                auto it = d.collisions.find(c.file);
                if (it != d.collisions.end()) place_polys(it->second, model_turn_index(0, c.rot), origin, out, 0);
            }
        for (const std::string& name : d.cfg.drans_cols) {   // Dran's arena tiles (all intact)
            auto it = d.collisions.find(name);
            if (it != d.collisions.end()) place_polys(it->second, model_turn_index(0, 0), origin, out, 0);
        }
        return out;
    }
    const int cx = int(x / kCellSize), cz = int(z / kCellSize);   // fptosi: truncation like the game
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx) {
            const int gx = cx + dx, gy = cz + dy;
            if (gx < 0 || gx >= kGrid || gy < 0 || gy >= kGrid) continue;
            const MapCell& cell = layout.at(gx, gy);
            if (cell.part < 0 || size_t(cell.part) >= d.cfg.parts.size()) continue;
            const float pos[3] = {float(gx) * kCellSize, 0.0f, float(gy) * kCellSize};
            for (const PartRef& c : d.cfg.parts[size_t(cell.part)].collisions) {
                auto it = d.collisions.find(c.file);
                if (it == d.collisions.end()) continue;
                const int skip = size_t(gy * kGrid + gx) < layout.door_open.size() ? int(layout.door_open[size_t(gy * kGrid + gx)]) : 0;
                place_polys(it->second, model_turn_index(cell.rot, c.rot), pos, out, skip);
            }
        }
    return out;
}

}  // namespace dccore

namespace dccore {

std::vector<CCPoly> floor_camera_polys(const Dungeon& d, const FloorLayout& layout, float x, float z) {
    std::vector<CCPoly> out;
    if (layout.fixed) {
        const float origin[3] = {0, 0, 0};
        for (const PartDef& part : d.cfg.parts)
            for (const PartRef& c : part.cameras) {
                auto it = d.cam_meshes.find(c.file);
                if (it != d.cam_meshes.end()) place_polys(it->second, model_turn_index(0, c.rot), origin, out);
            }
        return out;
    }
    for (int gy = 0; gy < kGrid; ++gy)
        for (int gx = 0; gx < kGrid; ++gx) {
            const MapCell& cell = layout.at(gx, gy);
            if (cell.part < 0 || size_t(cell.part) >= d.cfg.parts.size()) continue;
            const float cx = float(gx) * kCellSize - x, cz = float(gy) * kCellSize - z;
            if (std::sqrt(cx * cx + cz * cz) > 240.0f) continue;
            const float pos[3] = {float(gx) * kCellSize, 0.0f, float(gy) * kCellSize};
            for (const PartRef& c : d.cfg.parts[size_t(cell.part)].cameras) {
                auto it = d.cam_meshes.find(c.file);
                if (it == d.cam_meshes.end()) continue;
                place_polys(it->second, model_turn_index(cell.rot, c.rot), pos, out);
            }
        }
    return out;
}

}  // namespace dccore

namespace dccore {

std::vector<FloorMarker> floor_markers(const Dungeon& d, const FloorLayout& layout) {
    std::vector<FloorMarker> out;
    const int cells = layout.fixed ? int(d.cfg.parts.size()) : kGrid * kGrid;
    for (int ci = 0; ci < cells; ++ci) {
        const int gx = layout.fixed ? 0 : ci % kGrid, gy = layout.fixed ? 0 : ci / kGrid;
        {
            MapCell fixed_cell;
            if (layout.fixed) fixed_cell.part = ci;
            const MapCell& cell = layout.fixed ? fixed_cell : layout.at(gx, gy);
            if (cell.part < 0 || size_t(cell.part) >= d.cfg.parts.size()) continue;
            const PartDef& part = d.cfg.parts[size_t(cell.part)];
            for (const PartMarker& m : part.markers) {
                // the marker is an (empty) object of one of the part's collision meshes or models; its frame sits under that file's own turn
                auto emit = [&](const std::array<float, 3>& p, int file_rot) {
                    const int turn = ((model_turn_index(cell.rot, file_rot) % 4) + 4) % 4;
                    float c = 1, s2 = 0;
                    switch (turn) {
                        case 1: c = 0; s2 = -1; break;
                        case 2: c = -1; s2 = 0; break;
                        case 3: c = 0; s2 = 1; break;
                    }
                    FloorMarker fmk;
                    fmk.cell_x = gx; fmk.cell_y = gy; fmk.marker = m;
                    fmk.pos[0] = p[0] * c + p[2] * s2 + float(gx) * kCellSize;
                    fmk.pos[1] = p[1];
                    fmk.pos[2] = -p[0] * s2 + p[2] * c + float(gy) * kCellSize;
                    out.push_back(fmk);
                };
                bool found = false;
                for (const PartRef& cr : part.collisions) {
                    auto it = d.collisions.find(cr.file);
                    if (it == d.collisions.end()) continue;
                    for (const auto& fr : it->second.frames)
                        if (fr.first == m.name) { emit(fr.second, cr.rot); found = true; break; }
                    if (found) break;
                }
                if (found) continue;
                for (const PartModelRef& mr : part.models) {
                    auto fm = d.marker_frames.find(mr.file);
                    if (fm == d.marker_frames.end()) continue;
                    auto it = fm->second.find(m.name);
                    if (it == fm->second.end()) continue;
                    emit(it->second, mr.rot);
                    break;
                }
            }
        }
    }
    return out;
}

}  // namespace dccore

namespace dccore {

std::vector<std::array<float, 3>> floor_fires(const Dungeon& d, const FloorLayout& layout) {
    std::vector<std::array<float, 3>> out;
    const int cells = layout.fixed ? int(d.cfg.parts.size()) : kGrid * kGrid;
    for (int ci = 0; ci < cells; ++ci) {
        {
            const int gx = layout.fixed ? 0 : ci % kGrid, gy = layout.fixed ? 0 : ci / kGrid;
            MapCell fixed_cell;
            if (layout.fixed) fixed_cell.part = ci;
            const MapCell& cell = layout.fixed ? fixed_cell : layout.at(gx, gy);
            if (cell.part < 0 || size_t(cell.part) >= d.cfg.parts.size()) continue;
            const PartDef& part = d.cfg.parts[size_t(cell.part)];
            if (part.fires.empty()) continue;
            const int file_rot = part.models.empty() ? 0 : part.models[0].rot;
            const int turn = ((model_turn_index(cell.rot, file_rot) % 4) + 4) % 4;
            float c = 1, s2 = 0;
            switch (turn) {
                case 1: c = 0; s2 = -1; break;
                case 2: c = -1; s2 = 0; break;
                case 3: c = 0; s2 = 1; break;
            }
            for (const auto& p : part.fires)
                out.push_back({p[0] * c + p[2] * s2 + float(gx) * kCellSize, p[1], -p[0] * s2 + p[2] * c + float(gy) * kCellSize});
        }
    }
    return out;
}

}  // namespace dccore

namespace dccore {

// The layout of a fixed map: nothing but the flag (every part of the cfg stands once at the origin).
FloorLayout make_fixed_layout(int map_no) {
    FloorLayout l;
    l.fixed = true;
    l.map_no = map_no;
    return l;
}

// Where the player enters a fixed map: the frame called "in" of the parts (the landing at the entrance); the origin when there is none.
std::array<float, 3> fixed_start(const Dungeon& d) {
    for (const PartDef& part : d.cfg.parts)
        for (const PartModelRef& m : part.models) {
            auto fm = d.marker_frames.find(m.file);
            if (fm == d.marker_frames.end()) continue;
            auto it = fm->second.find("in");
            if (it != fm->second.end()) return {it->second[0] + m.offset[0], it->second[1] + m.offset[1], it->second[2] + m.offset[2]};
        }
    return {0, 0, 0};
}

}  // namespace dccore

namespace dccore {

bool find_floor_frame(const Dungeon& d, const FloorLayout& layout, const std::string& name, float pos[3], float* yaw) {
    const int cells = layout.fixed ? int(d.cfg.parts.size()) : kGrid * kGrid;
    for (int ci = 0; ci < cells; ++ci) {
        const int gx = layout.fixed ? 0 : ci % kGrid, gy = layout.fixed ? 0 : ci / kGrid;
        MapCell fixed_cell;
        if (layout.fixed) fixed_cell.part = ci;
        const MapCell& cell = layout.fixed ? fixed_cell : layout.at(gx, gy);
        if (cell.part < 0 || size_t(cell.part) >= d.cfg.parts.size()) continue;
        const PartDef& part = d.cfg.parts[size_t(cell.part)];
        auto place = [&](const std::array<float, 3>& p, int file_rot) {
            const int turn = ((model_turn_index(cell.rot, file_rot) % 4) + 4) % 4;
            float c = 1, s2 = 0;
            switch (turn) {
                case 1: c = 0; s2 = -1; break;
                case 2: c = -1; s2 = 0; break;
                case 3: c = 0; s2 = 1; break;
            }
            pos[0] = p[0] * c + p[2] * s2 + float(gx) * kCellSize;
            pos[1] = p[1];
            pos[2] = -p[0] * s2 + p[2] * c + float(gy) * kCellSize;
            if (yaw) *yaw = std::atan2(-s2, c);   // where the part's +z points
        };
        for (const PartRef& cr : part.collisions) {
            auto it = d.collisions.find(cr.file);
            if (it == d.collisions.end()) continue;
            for (const auto& fr : it->second.frames)
                if (fr.first == name) { place(fr.second, cr.rot); return true; }
        }
        for (const PartModelRef& mr : part.models) {
            auto fm = d.marker_frames.find(mr.file);
            if (fm == d.marker_frames.end()) continue;
            auto it = fm->second.find(name);
            if (it == fm->second.end()) {   // the model's object names carry draw flags after a double underscore (d02mob__v, light_on__c, udoor__s ...): the game searches by the name before them
                for (it = fm->second.begin(); it != fm->second.end(); ++it) {
                    const size_t us = it->first.find("__");
                    if (us != std::string::npos && it->first.compare(0, us, name) == 0) break;
                }
            }
            if (it != fm->second.end()) { place(it->second, mr.rot); return true; }
        }
    }
    return false;
}

}  // namespace dccore
