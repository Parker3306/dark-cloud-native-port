#include "dccore/character.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <unordered_map>

namespace dccore {

std::vector<std::string> cfg_strings(View text, const std::string& keyword) {
    std::vector<std::string> out;
    std::string s(reinterpret_cast<const char*>(text.data()), text.size());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;
        size_t c = line.find("//");
        if (c != std::string::npos) line.resize(c);
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
        if (line.compare(b, keyword.size(), keyword) != 0) continue;
        size_t after = b + keyword.size();
        if (after < line.size() && !std::isspace(static_cast<unsigned char>(line[after]))) continue;  // a longer keyword
        size_t q1 = line.find('"', after);
        size_t q2 = (q1 == std::string::npos) ? q1 : line.find('"', q1 + 1);
        if (q2 != std::string::npos) out.push_back(line.substr(q1 + 1, q2 - q1 - 1));
    }
    return out;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ends_with(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

struct V3 { double x, y, z; };

V3 xform_point(const std::array<double, 16>& m, const Vec4& v) {
    return {v.x * m[0] + v.y * m[4] + v.z * m[8] + m[12], v.x * m[1] + v.y * m[5] + v.z * m[9] + m[13], v.x * m[2] + v.y * m[6] + v.z * m[10] + m[14]};
}

V3 xform_dir(const std::array<double, 16>& m, const Vec4& v) {
    V3 r{v.x * m[0] + v.y * m[4] + v.z * m[8], v.x * m[1] + v.y * m[5] + v.z * m[9], v.x * m[2] + v.y * m[6] + v.z * m[10]};
    double l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    if (l > 1e-12) { r.x /= l; r.y /= l; r.z /= l; }
    return r;
}

// Builds the surfaces of one object. Vertices stay in the object's own space; `world` (the rest-pose world matrix) is only used to decide the
// winding, so that front faces are clockwise as seen from outside in the rest pose (Godot's convention).
Part build_part(const Object& o, int index, const std::array<double, 16>& world) {
    const Mesh& m = o.mesh;
    Part part;
    part.name = o.name;
    part.object = index;

    struct Key { int32_t p, n, uv; bool operator==(const Key& k) const { return p == k.p && n == k.n && uv == k.uv; } };
    struct KeyHash { size_t operator()(const Key& k) const { return (size_t(uint32_t(k.p)) * 73856093u) ^ (size_t(uint32_t(k.n)) * 19349663u) ^ (size_t(uint32_t(k.uv)) * 83492791u); } };

    auto valid = [&](const Triangle& t) {
        for (int k = 0; k < 3; ++k)
            if (size_t(t.pos[k]) >= m.positions.size() || size_t(t.nrm[k]) >= m.normals.size() || size_t(t.uv[k]) >= m.uvs.size()) return false;
        return true;
    };

    std::vector<std::pair<int, std::vector<Triangle>>> by_group;  // (material, triangles)
    double facing = 0;
    int current_material = -1;
    for (const Group& g : m.groups) {
        if (g.material != -1) current_material = g.material;
        auto tris = triangulate(g, m.stride);
        for (const Triangle& t : tris) {
            if (!valid(t)) continue;
            V3 a = xform_point(world, m.positions[t.pos[0]]), b = xform_point(world, m.positions[t.pos[1]]), c = xform_point(world, m.positions[t.pos[2]]);
            V3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
            V3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            V3 n0 = xform_dir(world, m.normals[t.nrm[0]]), n1 = xform_dir(world, m.normals[t.nrm[1]]), n2 = xform_dir(world, m.normals[t.nrm[2]]);
            facing += cr.x * (n0.x + n1.x + n2.x) + cr.y * (n0.y + n1.y + n2.y) + cr.z * (n0.z + n1.z + n2.z);
        }
        by_group.emplace_back(current_material, std::move(tris));
    }
    const bool flip = facing > 0;  // CCW around the normals -> reverse for Godot's clockwise front faces

    std::map<int, size_t> surface_of;               // material index -> index in part.surfaces
    std::vector<std::unordered_map<Key, uint32_t, KeyHash>> dedupe;
    for (auto& [material, tris] : by_group) {
        auto it = surface_of.find(material);
        if (it == surface_of.end()) {
            it = surface_of.emplace(material, part.surfaces.size()).first;
            part.surfaces.emplace_back();
            part.surfaces.back().texture = material >= 0 ? m.texture_name(size_t(material)) : "";
            dedupe.emplace_back();
        }
        Surface& s = part.surfaces[it->second];
        auto& map = dedupe[it->second];
        for (const Triangle& t : tris) {
            if (!valid(t)) continue;
            uint32_t idx[3];
            for (int k = 0; k < 3; ++k) {
                Key key{t.pos[k], t.nrm[k], t.uv[k]};
                auto f = map.find(key);
                if (f == map.end()) {
                    const Vec4& p = m.positions[key.p];
                    Vec4 nn = m.normals[key.n];
                    double l = std::sqrt(double(nn.x) * nn.x + double(nn.y) * nn.y + double(nn.z) * nn.z);
                    if (l > 1e-12) { nn.x = float(nn.x / l); nn.y = float(nn.y / l); nn.z = float(nn.z / l); }
                    const Vec4& uv = m.uvs[key.uv];
                    f = map.emplace(key, uint32_t(s.positions.size() / 3)).first;
                    s.positions.insert(s.positions.end(), {p.x, p.y, p.z});
                    s.normals.insert(s.normals.end(), {nn.x, nn.y, nn.z});
                    s.uvs.insert(s.uvs.end(), {uv.x, uv.y});
                    s.source.push_back(uint32_t(key.p));
                }
                idx[k] = f->second;
            }
            if (flip) std::swap(idx[1], idx[2]);
            s.indices.insert(s.indices.end(), {idx[0], idx[1], idx[2]});
        }
    }
    return part;
}

// Up to 4 strongest bones per vertex, normalised to sum 1 (the game's weights are percentages that sum to 100).
void assign_skin(Part& part, const std::map<uint32_t, std::map<uint32_t, float>>& weights) {
    for (Surface& s : part.surfaces) {
        const size_t n = s.source.size();
        s.bones.assign(n * 4, 0);
        s.weights.assign(n * 4, 0.0f);
        for (size_t v = 0; v < n; ++v) {
            auto it = weights.find(s.source[v]);
            std::vector<std::pair<float, uint32_t>> w;
            if (it != weights.end())
                for (const auto& [bone, pct] : it->second)
                    if (pct > 0) w.emplace_back(pct, bone);
            std::sort(w.begin(), w.end(), [](auto& a, auto& b) { return a.first > b.first; });
            if (w.size() > 4) w.resize(4);
            float sum = 0;
            for (auto& x : w) sum += x.first;
            if (w.empty()) { s.bones[v * 4] = int32_t(part.object); s.weights[v * 4] = 1.0f; continue; }  // unweighted vertex: follow the object itself
            for (size_t k = 0; k < w.size(); ++k) {
                s.bones[v * 4 + k] = int32_t(w[k].second);
                s.weights[v * 4 + k] = w[k].first / sum;
            }
        }
    }
}

}  // namespace

Part build_part_public(const Object& o, int index, const std::array<double, 16>& world) { return build_part(o, index, world); }

Character load_character_parts(View mds_data, View bbp_data, View mot_data, View wgt_data, View cfg_text, const std::string& model_name) {
    Character ch;
    ch.model_name = model_name;
    if (!cfg_text.empty()) ch.motions = cfg_motion_table(cfg_text);
    Mds mds = parse_mds(mds_data);
    auto world = world_matrices(mds);
    std::vector<Mat4> bbp;
    std::map<uint32_t, std::map<uint32_t, float>> weights;
    std::set<uint32_t> targets;
    if (!bbp_data.empty()) bbp = parse_bbp(bbp_data);
    if (!mot_data.empty()) ch.tracks = parse_mot(mot_data);
    if (!wgt_data.empty()) {
        auto recs = parse_wgt(wgt_data);
        for (const auto& r : recs) targets.insert(r.target);
        weights = vertex_weights(recs);
    }
    ch.skeleton = make_skeleton(mds, bbp);
    for (size_t i = 0; i < mds.objects.size(); ++i) {
        Joint j;
        j.name = mds.objects[i].name;
        j.parent = mds.objects[i].parent;
        j.world = world[i];
        ch.joints.push_back(std::move(j));
        if (!mds.objects[i].has_mesh) continue;
        Part part = build_part(mds.objects[i], int(i), world[i]);
        if (targets.count(uint32_t(i))) {
            part.skinned = true;
            assign_skin(part, weights);
            part.skin_root = std::max(0, mds.objects[i].parent);
        }
        ch.parts.push_back(std::move(part));
    }
    return ch;
}

namespace {
// `MOTION 0, "c01dbas.mot", "c01dbas.bbp", "c01dbas.wgt"`: the files of the motion set when they do not carry the model's stem (the village packs of the hero are named that way)
std::vector<std::string> cfg_motion_files(View text) {
    std::vector<std::string> out;
    std::string s(reinterpret_cast<const char*>(text.data()), text.size());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;
        size_t c = line.find("//");
        if (c != std::string::npos) line.resize(c);
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
        if (line.compare(b, 6, "MOTION") != 0 || (b + 6 < line.size() && !std::isspace(static_cast<unsigned char>(line[b + 6])))) continue;
        size_t q = b + 6;
        for (;;) {
            const size_t q1 = line.find('"', q);
            if (q1 == std::string::npos) break;
            const size_t q2 = line.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            out.push_back(line.substr(q1 + 1, q2 - q1 - 1));
            q = q2 + 1;
        }
        break;
    }
    return out;
}
}  // namespace

Character load_character(const Pack& pack, const std::string& cfg_entry) {
    Character ch;
    std::string mds_name, img_name;
    const PackEntry* cfg = cfg_entry.empty() ? pack.find("base.cfg") : pack.find(cfg_entry);
    if (!cfg && cfg_entry.empty()) cfg = pack.find("info.cfg");   // monster packs name their config info.cfg
    if (!cfg && cfg_entry.empty())           // effect packs call it <name>.cfg
        for (const auto& e : pack.entries())
            if (ends_with(lower(e.name), ".cfg")) { cfg = pack.find(e.name); break; }
    if (cfg) {
        View text = pack.data(*cfg);
        auto models = cfg_strings(text, "MODEL");
        auto imgs = cfg_strings(text, "IMG");
        if (!models.empty()) mds_name = models.front();
        if (!imgs.empty()) img_name = imgs.front();
        ch.motions = cfg_motion_table(text);
    }
    if (mds_name.empty() || !pack.find(mds_name)) {
        mds_name.clear();
        for (const auto& e : pack.entries()) {
            std::string n = lower(e.name);
            if (ends_with(n, ".mds") && n.find("hand") == std::string::npos && n.size() > 5 && n[n.size() - 5] != 's') { mds_name = e.name; break; }
        }
    }
    if (img_name.empty() || !pack.find(img_name)) {
        img_name.clear();
        for (const auto& e : pack.entries())
            if (ends_with(lower(e.name), ".img") && lower(e.name).find("w_h") == std::string::npos) { img_name = e.name; break; }
    }
    if (mds_name.empty()) throw Error("no model found in the pack");
    ch.model_name = mds_name;
    ch.img_name = img_name;

    Mds mds = parse_mds(pack.get(mds_name));
    auto world = world_matrices(mds);

    // motion, bind pose and skin weights share the model's file stem (c01d.mds -> c01d.mot / .bbp / .wgt)
    const std::string stem = mds_name.substr(0, mds_name.size() - 4);
    std::vector<Mat4> bbp;
    std::map<uint32_t, std::map<uint32_t, float>> weights;
    std::set<uint32_t> targets;
    std::string mot_name = stem + ".mot", bbp_name = stem + ".bbp", wgt_name = stem + ".wgt";
    if (cfg) {   // (a pack whose motion files are named in its cfg: MOTION 0, "c01dbas.mot", "c01dbas.bbp", "c01dbas.wgt")
        const std::vector<std::string> files = cfg_motion_files(pack.data(*cfg));
        if (files.size() >= 3 && pack.find(files[0]) && !pack.find(mot_name)) { mot_name = files[0]; bbp_name = files[1]; wgt_name = files[2]; }
    }
    if (const PackEntry* e = pack.find(bbp_name)) bbp = parse_bbp(pack.data(*e));
    if (const PackEntry* e = pack.find(mot_name)) ch.tracks = parse_mot(pack.data(*e));
    if (const PackEntry* e = pack.find(wgt_name)) {
        auto recs = parse_wgt(pack.data(*e));
        for (const auto& r : recs) targets.insert(r.target);
        weights = vertex_weights(recs);
    }
    ch.skeleton = make_skeleton(mds, bbp);

    for (size_t i = 0; i < mds.objects.size(); ++i) {
        Joint j;
        j.name = mds.objects[i].name;
        j.parent = mds.objects[i].parent;
        j.world = world[i];
        ch.joints.push_back(std::move(j));
        if (!mds.objects[i].has_mesh) continue;
        Part part = build_part(mds.objects[i], int(i), world[i]);
        if (targets.count(uint32_t(i))) {
            part.skinned = true;
            assign_skin(part, weights);
            part.skin_root = std::max(0, mds.objects[i].parent);
        }
        ch.parts.push_back(std::move(part));
    }
    if (!img_name.empty()) {
        View img = pack.get(img_name);
        for (const auto& t : img_textures(img)) ch.textures[t.name] = Tim2(img, t.offset).decode(false, t.swizzled);
    }
    return ch;
}

bool cfg_has_model(View text) { return !cfg_strings(text, "MODEL").empty(); }

int add_motion_sets(Character& ch, const Pack& pack, View text) {
    std::string s(reinterpret_cast<const char*>(text.data()), text.size());
    int added = 0;
    bool in_motion = false, in_keys = false;
    std::string mot_name;
    int key_no = 0;
    std::vector<MotionEntry> keys;
    size_t pos = 0;
    auto flush = [&]() {
        if (!in_motion) return;
        const PackEntry* e = mot_name.empty() ? nullptr : pack.find(mot_name);
        if (e) {
            ch.extra_sets.push_back(parse_mot(pack.data(*e)));
            const int set = int(ch.extra_sets.size()) - 1;
            for (size_t i = 0; i < keys.size(); ++i) {
                ch.extra_motions[key_no + int(i)] = {set, keys[i]};
                ++added;
            }
        }
        in_motion = in_keys = false;
        keys.clear();
        mot_name.clear();
    };
    while (pos < s.size()) {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;
        const size_t c = line.find("//");
        if (c != std::string::npos) line.resize(c);
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
        auto token_is = [&](const char* kw) {
            const size_t n = std::strlen(kw);
            return line.compare(b, n, kw) == 0 && (b + n >= line.size() || !(std::isalnum(static_cast<unsigned char>(line[b + n])) || line[b + n] == '_'));
        };
        if (token_is("MOTION_END")) { flush(); continue; }
        if (token_is("MOTION")) {
            flush();
            in_motion = true;
            key_no = 0;
            const size_t q1 = line.find('"', b);
            const size_t q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            if (q2 != std::string::npos) mot_name = line.substr(q1 + 1, q2 - q1 - 1);
            continue;
        }
        if (!in_motion) continue;
        if (token_is("KEY_START")) { key_no = std::atoi(line.c_str() + b + 9); in_keys = true; continue; }
        if (!in_keys || !token_is("KEY")) continue;
        std::string rest = line.substr(b + 3);
        for (char& ch2 : rest) if (ch2 == ',') ch2 = ' ';
        float v[3] = {0, 0, 0};
        int n = 0;
        const char* p = rest.c_str();
        char* end = nullptr;
        while (n < 3) {
            const float x = std::strtof(p, &end);
            if (end == p) break;
            v[n++] = x;
            p = end;
        }
        if (n == 3) keys.push_back({v[0], v[1], v[2]});
    }
    flush();
    return added;
}

}  // namespace dccore
