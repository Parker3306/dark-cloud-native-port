#include "dccore/mapinfo.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace dccore {

namespace {

enum ArgType { S, I, F };

struct Tag {
    const char* name;
    std::vector<ArgType> types;
};

std::vector<ArgType> rep(ArgType t, int n) { return std::vector<ArgType>(size_t(n), t); }
std::vector<ArgType> cat(std::vector<ArgType> a, const std::vector<ArgType>& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

const std::vector<Tag>& tags() {
    static const std::vector<Tag> t = {
        {"SCN", {S}}, {"LIGHT_NO", {I}}, {"AMBIENT", {F, F, F}}, {"LIGHT_C", {F, F, F, F, F, F, I}},
        {"FOG", {F, F, I, I, I, F, F}}, {"BG_COL", {F, F, F}}, {"BG_COL2", {F, F, F}}, {"DOF", {F, F, I, F, F, I, I}},
        {"CD", {S}}, {"GRD_IMG", {S, I}}, {"BLD_IMG", {S, I}}, {"SKY_IMG", {I, S, I}}, {"SUN_IMG", {S, I}},
        {"WATER_IMG", {S, I}}, {"FIRE_IMG", {S, I}}, {"FLER_IMG", {S, I}}, {"IMG", {I, S, I}}, {"SKY", {I, S, I}},
        {"SUN", {I, S, I}}, {"GROUND", cat(rep(S, 8), rep(F, 6))}, {"BUILD", cat(rep(S, 8), rep(F, 6))}, {"WATER", cat(rep(S, 8), rep(F, 6))},
        {"WATER_SURFACE", cat(cat({S, I, I}, rep(F, 13)), rep(I, 6))}, {"WATER_SHAKE", {I, I, F, F}},
        {"EDITAREA", {I, S, I, I, F, F, F, F, F}}, {"BLD_PARTS", {I, S, I, F}}, {"GRD_PARTS", {I, S, I, F}},
        {"PARTS_INFO", {I, I, S, I, I, S, I, S, I, S, I, S, I, S, I, S}}, {"ROAD_PARTS", {I, S, I, F}},
        {"ROAD", {I, S, I, F}}, {"RIVER_PARTS", {I, S, I, F}}, {"RIVER", {I, S, I, F, S}},
        {"BRIDGE_PARTS", {I, S, I, F}}, {"LAKE_PARTS", {I, S, I, F, S}}, {"ON_RIVER_PARTS", {I, S, I, F, S}},
        {"OBJ_ANIME", cat({I, I, S}, rep(F, 9))}, {"FIRE", cat({S}, rep(F, 6))}, {"FLAME", cat({S}, rep(F, 6))}, {"BRIGHT", cat({S}, rep(F, 6))},
        {"OBJECT_TIMER", {S, F, F}}, {"ENTRANCE", cat({S}, rep(F, 9))}, {"MAPJUMP", {I, S}},
        {"PEOPLE", {S, I, I, I, I, F, F, F, F, F}}, {"TIME_TABLE_NO", {I}}, {"TIME_TABLE", rep(I, 7)}, {"TIME_STOP", {}},
        {"SKY_FOLLOW", {I, I, I}}, {"SHADOW_LEVEL", {I, F, F, I, I}}, {"EDITAREA_RECT", rep(F, 6)}, {"BGM_NO", {I}},
        {"SOUND_SET", {I}}, {"REVERBE", {S, I, S, I}}, {"MOTION_PARTS", cat({S}, rep(F, 6))}, {"PEOPLE2", {S, I, I, F}},
        {"SE_AMBIENT_OFF", {}}, {"WIND", {F, F, F, F}}, {"TALK_EVENT", {I, I}}, {"CHARA_AMBIENT", {I, F, F, F}},
        {"TALK_ROT", {I}}, {"TALK_DIR", {I}}, {"PEOPLE_LIST", rep(I, 15)},
    };
    return t;
}

const std::vector<Tag>& interior_tags() {   // LoadInfo (0x19fb70): the table at 0x26A7F0
    static const std::vector<Tag> t = {
        {"AMBIENT", {F, F, F}}, {"LIGHT_C", {F, F, F, F, F, F, I}}, {"FOG", {F, F, I, I, I, F, F}}, {"BG_COL", {F, F, F}},
        {"PROJECTION", {F}}, {"PEOPLE", {I, F, F, F, F}}, {"CD", {S}}, {"OBJ_ANIME", cat({I, I, S}, rep(F, 9))},
        {"FIRE", {S, F, F, F, F}}, {"FLAME", {S, F, F, F, F}}, {"BRIGHT", {S, F, F, F, F}}, {"DEBUG", {}},
        {"MOTION_PARTS", cat({S}, rep(F, 9))}, {"WATER_SURFACE", cat(cat({S, I, I}, rep(F, 13)), rep(I, 6))}, {"WATER_SHAKE", {I, I, F, F}},
    };
    return t;
}

struct Arg {
    std::string s;
    double f = 0;
    int i = 0;
};

bool ws(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string preprocess(View in) {
    std::string b(reinterpret_cast<const char*>(in.data()), in.size());
    const size_t n = b.size();
    size_t i = 0;
    while (i < n) {
        if (b[i] == '/' && i + 1 < n && b[i + 1] == '/') {
            while (i < n && b[i] != '\n' && b[i] != '\r') b[i++] = ' ';
            continue;
        }
        if (b[i] == '/' && i + 1 < n && b[i + 1] == '*') {
            while (i < n) {
                if (b[i] == '*' && i + 1 < n && b[i + 1] == '/') { b[i] = b[i + 1] = ' '; break; }
                b[i++] = ' ';
            }
            continue;
        }
        ++i;
    }
    return b;
}

}  // namespace

MapInfo parse_mapinfo(View text, bool interior) {
    const std::string buf = preprocess(text);
    const size_t n = buf.size();
    size_t pos = 0;
    const auto& table = interior ? interior_tags() : tags();
    auto skipspace = [&]() {
        while (pos < n && ws(uint8_t(buf[pos]))) ++pos;
        return pos < n;
    };

    MapInfo m;
    std::string cur_dir;
    int light_no = 0, week = 0;
    MapOwned* owner = nullptr;
    PartsDef* now_parts = nullptr;
    PeopleDef* now_vill = nullptr;
    auto full = [&](const std::string& s) { return s.empty() ? s : cur_dir + s; };

    while (true) {
        if (!skipspace()) break;
        const size_t start = pos;
        while (pos < n && !ws(uint8_t(buf[pos]))) ++pos;
        if (pos >= n) break;
        const std::string word = buf.substr(start, pos - start);
        ++pos;
        int idx = -1;
        if (word[0] >= 'A' && word[0] <= 'Z')
            for (size_t k = 0; k < table.size(); ++k)
                if (word == table[k].name) { idx = int(k); break; }
        if (idx < 0) {
            if (word == "test") break;
            continue;
        }
        std::vector<Arg> a;
        for (ArgType t : table[size_t(idx)].types) {
            if (!skipspace()) throw Error("mapinfo: unexpected end in " + word);
            std::string tok;
            const char c = buf[pos++];
            if (c == ',') skipspace();
            else tok.push_back(c);
            while (pos < n) {
                const char d = buf[pos++];
                if (d == ',' || ws(uint8_t(d))) break;
                tok.push_back(d);
            }
            Arg ar;
            if (t == F) {
                for (char ch : tok) if (!((ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) throw Error("mapinfo: bad float in " + word);
                ar.f = std::atof(tok.c_str());
            } else if (t == I) {
                for (char ch : tok) if (!((ch >= '0' && ch <= '9') || ch == '-')) throw Error("mapinfo: bad integer in " + word);
                ar.i = std::atoi(tok.c_str());
            } else {
                const size_t q = tok.find('"', 1);
                if (tok.empty() || tok[0] != '"' || q == std::string::npos) throw Error("mapinfo: bad string in " + word);
                ar.s = tok.substr(1, q - 1);
            }
            a.push_back(std::move(ar));
        }
        auto f = [&](size_t k) { return float(a[k].f); };
        const std::string& nm = word;
        if (nm == "SCN") m.scn = a[0].s;
        else if (nm == "LIGHT_NO") light_no = std::min(std::max(a[0].i - 1, 0), 11);
        else if (nm == "AMBIENT") { for (int k = 0; k < 3; ++k) m.lights[size_t(light_no)].ambient[k] = f(size_t(k)); }
        else if (nm == "LIGHT_C") {
            const double x = a[0].f, y = a[1].f, z = a[2].f;
            double len = std::sqrt(x * x + y * y + z * z);
            if (len == 0) len = 1;
            const int k = a[6].i - 1;
            if (k >= 0 && k < 4) {
                MapLight& L = m.lights[size_t(light_no)];
                L.has_dir[k] = true;
                L.dir[k][0] = float(x / len); L.dir[k][1] = float(y / len); L.dir[k][2] = float(z / len);
                for (int j = 0; j < 3; ++j) L.color[k][j] = f(size_t(3 + j));
            }
        } else if (nm == "FOG") {
            MapLight& L = m.lights[size_t(light_no)];
            L.has_fog = true;
            L.fog[0] = f(0); L.fog[1] = f(1);
            for (int k = 0; k < 3; ++k) L.fog[2 + k] = float(a[size_t(2 + k)].i);
            L.fog[5] = f(5); L.fog[6] = f(6);
        } else if (nm == "BG_COL") {
            MapLight& L = m.lights[size_t(light_no)];
            L.has_bg = true;
            for (int k = 0; k < 3; ++k) L.bg_top[k] = f(size_t(k));
        } else if (nm == "BG_COL2") {
            MapLight& L = m.lights[size_t(light_no)];
            for (int k = 0; k < 3; ++k) L.bg_bottom[k] = f(size_t(k));
        } else if (nm == "DOF") {
            m.has_dof = true;
            m.dof[0] = f(0); m.dof[1] = f(1); m.dof[2] = float(a[2].i); m.dof[3] = f(3); m.dof[4] = f(4); m.dof[5] = float(a[5].i); m.dof[6] = float(a[6].i);
        } else if (nm == "CD") cur_dir = a[0].s;
        else if (nm == "GRD_IMG" || nm == "BLD_IMG" || nm == "SUN_IMG" || nm == "WATER_IMG" || nm == "FIRE_IMG" || nm == "FLER_IMG") {
            const int kind = nm == "GRD_IMG" ? 1 : nm == "BLD_IMG" ? 2 : nm == "SUN_IMG" ? 7 : nm == "WATER_IMG" ? 0x15 : nm == "FIRE_IMG" ? 0x18 : 0x17;
            m.textures.push_back({cur_dir, a[0].s, kind, a[1].i});
        } else if (nm == "SKY_IMG") {
            if (a[0].i >= 1 && a[0].i <= 4) m.textures.push_back({cur_dir, a[1].s, a[0].i + 2, a[2].i});
        } else if (nm == "IMG") m.textures.push_back({cur_dir, a[1].s, a[0].i, a[2].i});
        else if (nm == "SKY" || nm == "SUN") {
            const bool sky = nm == "SKY";
            if (a[0].i >= 1 && a[0].i <= (sky ? 5 : 4)) {
                SkyDef& d = (sky ? m.skies : m.suns)[a[0].i];
                d = SkyDef();
                d.file = full(a[1].s);
                d.flag = a[2].i;
                owner = &d;
            }
        } else if (nm == "GROUND" || nm == "BUILD" || nm == "WATER") {
            MapObject o;
            o.kind = nm;
            for (int k = 0; k < 8; ++k) o.names.push_back(full(a[size_t(k)].s));
            for (int k = 0; k < 3; ++k) { o.pos[k] = f(size_t(8 + k)); o.rot_deg[k] = f(size_t(11 + k)); }
            m.objects.push_back(std::move(o));
            owner = &m.objects.back();
        } else if (nm == "WATER_SURFACE") {
            WaterSurfaceDef w;
            w.texture = a[0].s; w.nx = a[1].i; w.nz = a[2].i;
            for (int k = 0; k < 3; ++k) { w.p0[k] = f(size_t(3 + k)); w.p1[k] = f(size_t(6 + k)); w.origin[k] = f(size_t(9 + k)); w.rgb[k] = float(a[size_t(16 + k)].i); w.extra[k] = a[size_t(19 + k)].i; }
            for (int k = 0; k < 4; ++k) w.params[k] = f(size_t(12 + k));
            m.water_surfaces.push_back(std::move(w));
        } else if (nm == "WATER_SHAKE") {
            if (!m.water_surfaces.empty()) m.water_surfaces.back().shakes.push_back({float(a[0].i), float(a[1].i), f(2), f(3)});
        } else if (nm == "EDITAREA") {
            EditAreaDef e;
            e.grid = full(a[1].s); e.width = a[2].i; e.height = a[3].i; e.unit = f(4); e.unit_alt = f(5);
            for (int k = 0; k < 3; ++k) e.origin[k] = f(size_t(6 + k));
            m.edit_areas[a[0].i] = e;
        } else if (nm == "BLD_PARTS" || nm == "GRD_PARTS" || nm == "ROAD_PARTS" || nm == "RIVER_PARTS" || nm == "BRIDGE_PARTS" || nm == "LAKE_PARTS" || nm == "ON_RIVER_PARTS") {
            PartsDef p;
            p.index = a[0].i; p.directive = nm; p.file = a[1].s.empty() ? "" : full(a[1].s); p.flag = a[2].i; p.scale = f(3);
            if (a.size() > 4) p.extra = a[4].s;
            PartsDef& slot = m.parts[p.index];
            slot = std::move(p);
            now_parts = &slot;
            owner = &slot;
        } else if (nm == "PARTS_INFO") {
            if (now_parts) {
                PartsInfoDef& d = now_parts->info;
                now_parts->has_info = true;
                d.width = a[0].i; d.height = a[1].i; d.grid = a[2].s; d.max_count = a[3].i;
                for (int k = 0; k < 6; ++k) d.elements[size_t(k)] = {a[size_t(4 + 2 * k)].i, a[size_t(5 + 2 * k)].s};
            }
        } else if (nm == "ROAD") m.roads[a[0].i] = {full(a[1].s), a[2].i, f(3)};
        else if (nm == "RIVER") m.rivers[a[0].i] = {full(a[1].s), a[2].i, f(3), full(a[4].s)};
        else if (nm == "OBJ_ANIME") {
            MapAnim an;
            an.kind = a[0].i; an.mode = a[1].i; an.target = a[2].s;
            for (int k = 0; k < 3; ++k) { an.init[k] = f(size_t(3 + k)); an.delta[k] = f(size_t(6 + k)); an.end[k] = f(size_t(9 + k)); }
            m.anims.push_back(std::move(an));
            if (owner) owner->anims.push_back(int(m.anims.size()));
        } else if (nm == "MAPJUMP") {
            MapEvent e;
            e.jump_id = a[0].i; e.map = a[1].s;
            m.events.push_back(std::move(e));
            if (owner) owner->events.push_back(int(m.events.size()));
        } else if (nm == "ENTRANCE") {
            if (!m.events.empty()) {
                MapEvent& e = m.events.back();
                e.has_door = true;
                const char c = a[0].s.empty() ? 0 : char(std::toupper(a[0].s[0]));
                e.side = c == 'L' ? -1 : c == 'R' ? 1 : 0;
                for (int k = 0; k < 3; ++k) { e.door_pos[k] = f(size_t(1 + k)); e.door_rot[k] = f(size_t(4 + k)); e.extent[k] = f(size_t(7 + k)); }
            }
        } else if (nm == "PEOPLE" && interior) {
            PeopleDef p;
            p.part = a[0].i;   // the villager number
            for (int k = 0; k < 3; ++k) p.pos[k] = f(size_t(1 + k));
            p.rot_y = f(4);
            m.people.push_back(std::move(p));
            now_vill = nullptr;
        } else if (nm == "PROJECTION") {
            m.projection = f(0);
        } else if (nm == "PEOPLE") {
            PeopleDef p;
            p.name = a[0].s; p.part = a[1].i; p.element = a[2].i; p.indoor_only = a[3].i; p.moves = a[4].i; p.speed = f(5);
            for (int k = 0; k < 3; ++k) p.pos[k] = f(size_t(6 + k));
            p.rot_y = f(9);
            m.people.push_back(std::move(p));
            now_vill = nullptr;
        } else if (nm == "PEOPLE2") {
            PeopleDef p;
            p.name = a[0].s; p.indoor_only = a[1].i; p.moves = a[2].i; p.speed = f(3);
            m.people.push_back(std::move(p));
            now_vill = &m.people.back();
        } else if (nm == "TALK_EVENT") {
            if (now_vill) { now_vill->has_talk_event = true; now_vill->talk_event[0] = a[0].i; now_vill->talk_event[1] = a[1].i; }
        } else if (nm == "TALK_ROT") { if (now_vill) now_vill->talk_rot = a[0].i; }
        else if (nm == "TALK_DIR") { if (now_vill) now_vill->talk_dir = a[0].i; }
        else if (nm == "TIME_TABLE_NO") week = a[0].i >= 0 && a[0].i <= 6 ? a[0].i : 0;
        else if (nm == "TIME_TABLE") {
            if (a[0].i >= 0) {
                std::array<int, 6> v{};
                for (int k = 0; k < 6; ++k) v[size_t(k)] = a[size_t(1 + k)].i;
                m.time_table[{week, a[0].i}] = v;
            }
        } else if (nm == "TIME_STOP") m.time_stop = true;
        else if (nm == "SKY_FOLLOW") { for (int k = 0; k < 3; ++k) m.sky_follow[k] = a[size_t(k)].i; }
        else if (nm == "EDITAREA_RECT") {
            std::array<float, 6> r{};
            for (int k = 0; k < 3; ++k) { r[size_t(k)] = std::min(f(size_t(k)), f(size_t(3 + k))); r[size_t(3 + k)] = std::max(f(size_t(k)), f(size_t(3 + k))); }
            m.edit_rects.push_back(r);
        } else if (nm == "BGM_NO") m.bgm = a[0].i;
        else if (nm == "SOUND_SET") m.sound_set = a[0].i;
        else if (nm == "REVERBE") { m.reverb[0] = a[0].s; m.reverb[1] = a[2].s; }
        else if (nm == "MOTION_PARTS") {
            MotionPartDef d;
            d.file = a[0].s.empty() ? "" : full(a[0].s);
            for (int k = 0; k < 3; ++k) { d.pos[k] = f(size_t(1 + k)); d.rot[k] = f(size_t(4 + k)); }
            m.motion_parts.push_back(std::move(d));
        } else if (nm == "SE_AMBIENT_OFF") m.se_ambient_off = true;
        else if (nm == "CHARA_AMBIENT") {
            if (a[0].i >= 1 && a[0].i <= 4) {
                m.has_chara_ambient[a[0].i - 1] = true;
                for (int k = 0; k < 3; ++k) m.chara_ambient[a[0].i - 1][k] = f(size_t(1 + k));
            }
        } else if (nm == "PEOPLE_LIST") { for (const Arg& x : a) m.people_list.push_back(x.i); }
        // everything else (FIRE, FLAME, BRIGHT, OBJECT_TIMER, SHADOW_LEVEL, WIND) is ignored or not needed yet
    }
    if (interior)
        for (size_t k = 1; k < m.lights.size(); ++k) m.lights[k] = m.lights[0];
    return m;
}

}  // namespace dccore
