#include "dc_opening.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <dccore/mds.h>
#include <dccore/tim2.h>

using namespace godot;

namespace {

constexpr float kPi = 3.14159265358979f;

Ref<ImageTexture> to_texture(const dccore::Image &im) {
    PackedByteArray data;
    data.resize(int64_t(im.rgba.size()));
    std::memcpy(data.ptrw(), im.rgba.data(), im.rgba.size());
    Ref<Image> img = Image::create_from_data(int(im.width), int(im.height), false, Image::FORMAT_RGBA8, data);
    img->generate_mipmaps();
    return ImageTexture::create_from_image(img);
}

Ref<ArrayMesh> mesh_of_surfaces(const std::vector<const dccore::Surface *> &list, const Vector3 &pivot, const std::function<Ref<StandardMaterial3D>(const std::string &)> &material_for) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    for (const dccore::Surface *sp : list) {
        const dccore::Surface &s = *sp;
        if (s.indices.empty()) continue;
        PackedVector3Array v, n;
        PackedVector2Array uv;
        PackedInt32Array idx;
        const int64_t nv = int64_t(s.positions.size() / 3);
        v.resize(nv); n.resize(nv); uv.resize(nv);
        for (int64_t i = 0; i < nv; ++i) {
            v.set(i, Vector3(s.positions[3 * i], s.positions[3 * i + 1], s.positions[3 * i + 2]) - pivot);
            n.set(i, Vector3(s.normals[3 * i], s.normals[3 * i + 1], s.normals[3 * i + 2]));
            uv.set(i, Vector2(s.uvs[2 * i], s.uvs[2 * i + 1]));
        }
        idx.resize(int64_t(s.indices.size()));
        for (size_t i = 0; i < s.indices.size(); ++i) idx.set(int64_t(i), int32_t(s.indices[i]));
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = v;
        arrays[Mesh::ARRAY_NORMAL] = n;
        arrays[Mesh::ARRAY_TEX_UV] = uv;
        arrays[Mesh::ARRAY_INDEX] = idx;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_material(mesh->get_surface_count() - 1, material_for(s.additive ? s.texture + "#add" : s.texture));
    }
    return mesh;
}

std::vector<std::string> split_cfg(const std::string &line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') { quoted = !quoted; continue; }
        if (!quoted && (c == ',' || c == ' ' || c == '\t' || c == '\r')) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

float fnum(const std::vector<std::string> &t, size_t i) { return i < t.size() ? float(std::atof(t[i].c_str())) : 0.0f; }

// the stage objects the script drives, by object number: the node of the camera clip that moves them (part A)
const char *const kNodeA[8] = {"c07a", "c08a", "c11a", "c09a", "c08a", "c08c", "p19a", "p17a"};

}  // namespace

void DcOpening::_bind_methods() {
    ClassDB::bind_method(D_METHOD("step_ticks", "n"), &DcOpening::step_ticks);
    ClassDB::bind_method(D_METHOD("set_auto_run", "on"), &DcOpening::set_auto_run);
    ClassDB::bind_method(D_METHOD("set_auto_key", "on"), &DcOpening::set_auto_key);
    ClassDB::bind_method(D_METHOD("press_ok"), &DcOpening::press_ok);
    ClassDB::bind_method(D_METHOD("skip"), &DcOpening::skip);
    ClassDB::bind_method(D_METHOD("get_ui"), &DcOpening::get_ui);
    ClassDB::bind_method(D_METHOD("get_environment"), &DcOpening::get_environment);
    ClassDB::bind_method(D_METHOD("take_events"), &DcOpening::take_events);
    ClassDB::bind_method(D_METHOD("get_message", "id"), &DcOpening::get_message);
    ClassDB::bind_method(D_METHOD("get_steps"), &DcOpening::get_steps);
    ClassDB::bind_method(D_METHOD("is_finished"), &DcOpening::is_finished);
    ClassDB::bind_method(D_METHOD("get_scene"), &DcOpening::get_scene);
    ClassDB::bind_method(D_METHOD("get_camera"), &DcOpening::get_camera);
    ClassDB::bind_method(D_METHOD("fast_forward", "steps"), &DcOpening::fast_forward);
    ClassDB::bind_method(D_METHOD("get_debug"), &DcOpening::get_debug);
    ClassDB::bind_method(D_METHOD("get_music_pack", "scene"), &DcOpening::get_music_pack);
}

void DcOpening::setup(dccore::Disc *disc) {
    disc_ = disc;
    const dccore::Bytes script = disc_->read("opdat/opening.scr");
    title_ = disc_->read_root("TITLE.BIN");
    sim_ = std::make_unique<dccore::OpeningSim>(dccore::parse_opening_script(std::string(script.begin(), script.end())), dccore::opening_sne_names(title_));
    if (disc_->contains("opdat/usa/fconv.bin")) text_ = dccore::MessageBank::parse(disc_->read("opdat/usa/fconv.bin"));
    for (int &m : obj_motion_shown_) m = -1;
    stage_ = memnew(Node3D);
    add_child(stage_);
    camera_ = memnew(Camera3D);
    camera_->set_near(6.0);
    camera_->set_far(65535.0);
    add_child(camera_);
    set_physics_process(true);
    load_scene(0);
}

String DcOpening::get_message(int id) const { return String::utf8(text_.get(id, {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"}).c_str()); }

Ref<StandardMaterial3D> DcOpening::material_for(const std::string &tex) {
    auto it = materials_.find(tex);
    if (it != materials_.end()) return it->second;
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_roughness(1.0f);
    m->set_metallic(0.0f);
    const bool additive = tex.size() > 4 && tex.compare(tex.size() - 4, 4, "#add") == 0;
    const std::string name = additive ? tex.substr(0, tex.size() - 4) : tex;
    auto t = textures_.find(name);
    if (additive) {
        m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
        m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        m->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
        if (t != textures_.end()) m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, to_texture(t->second));
    } else if (t != textures_.end()) {
        m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, to_texture(t->second));
        m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
        m->set_alpha_scissor_threshold(0.5f);
        m->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
    } else {
        m->set_albedo(Color(0.6f, 0.6f, 0.6f));
    }
    materials_[tex] = m;
    return m;
}

void DcOpening::add_images_from(const std::string &path) {
    if (!disc_->contains(path)) return;
    try {
        dccore::Bytes raw = disc_->read(path);
        dccore::Pack pk(std::move(raw));
        for (const dccore::PackEntry &e : pk.entries()) {
            const std::string &n = e.name;
            if (n.size() < 5 || n.compare(n.size() - 4, 4, ".img") != 0) continue;
            dccore::View v = pk.data(e);
            for (const dccore::ImgTexture &t : dccore::img_textures(v))
                if (!textures_.count(t.name)) textures_[t.name] = dccore::Tim2(v, t.offset).decode(false, t.swizzled);
        }
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcOpening images: ") + e.what());
    }
}

void DcOpening::clear_stage() {
    for (DcCharacter *&c : chars_) c = nullptr;
    dancers_.clear();
    spins_.clear();
    torches_.clear();
    for (int i = stage_->get_child_count() - 1; i >= 0; --i) {
        Node *c = stage_->get_child(i);
        stage_->remove_child(c);
        c->queue_free();
    }
    for (int &m : obj_motion_shown_) m = -1;
    effects_.clear();
}

void DcOpening::add_model(const std::string &path, const Vector3 &pos, const Vector3 &rot_deg, bool, const std::map<std::string, Vector3> &spins) {
    const std::vector<uint8_t> raw = read_model(path);
    if (raw.empty()) return;
    try {
        const dccore::Mds mds = dccore::parse_mds(raw, false);
        const dccore::StaticModel model = dccore::build_static_model(mds);
        Node3D *root = memnew(Node3D);
        const Basis b = Basis::from_euler(Vector3(rot_deg.x, rot_deg.y, rot_deg.z) * (kPi / 180.0f));
        root->set_basis(b);
        root->set_position(pos);
        stage_->add_child(root);
        auto mat = [this](const std::string &t) { return material_for(t); };
        std::map<std::string, std::vector<const dccore::Surface *>> by_object;
        std::vector<const dccore::Surface *> rest;
        for (const dccore::Surface &s : model.surfaces) {
            if (spins.count(s.object)) by_object[s.object].push_back(&s);
            else rest.push_back(&s);
        }
        if (!rest.empty()) {
            MeshInstance3D *mi = memnew(MeshInstance3D);
            mi->set_mesh(mesh_of_surfaces(rest, Vector3(), mat));
            root->add_child(mi);
        }
        for (auto &kv : by_object) {
            const dccore::Surface &s0 = *kv.second.front();
            const Vector3 pivot(s0.pivot[0], s0.pivot[1], s0.pivot[2]);
            Node3D *p = memnew(Node3D);
            p->set_position(pivot);
            root->add_child(p);
            MeshInstance3D *mi = memnew(MeshInstance3D);
            mi->set_mesh(mesh_of_surfaces(kv.second, pivot, mat));
            p->add_child(mi);
            spins_.push_back({p, spins.at(kv.first)});
        }
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcOpening model ") + path.c_str() + ": " + e.what());
    }
}

DcCharacter *DcOpening::make_character(const std::string &pack_path, const std::string &cfg_name, const std::string &mds_override) {
    try {
        dccore::Pack pk(disc_->read(pack_path));
        auto ch = std::make_shared<dccore::Character>();
        if (mds_override.empty()) {
            *ch = dccore::load_character(pk, cfg_name);
        } else {   // the mesh lives in another pack (the dancers): skeleton, skin and motions come from this one
            const std::string stem = cfg_name.substr(0, cfg_name.size() - 4);
            auto get = [&](const std::string &n) { const dccore::PackEntry *e = pk.find(n); return e ? pk.data(*e) : dccore::View(); };
            dccore::Pack mp(disc_->read("opdat/dungeon/dungeon.pak"));
            *ch = dccore::load_character_parts(mp.get(mds_override), get(stem + ".bbp"), get(stem + ".mot"), get(stem + ".wgt"), get(cfg_name), mds_override);
        }
        std::map<std::string, dccore::Image> used;
        for (const dccore::Part &p : ch->parts)
            for (const dccore::Surface &s : p.surfaces) {
                auto t = textures_.find(s.texture);
                if (t != textures_.end()) used[s.texture] = t->second;
            }
        for (auto &kv : used) ch->textures[kv.first] = std::move(kv.second);
        DcCharacter *node = memnew(DcCharacter);
        Dictionary info;
        node->setup(ch, info);
        node->set_playing(false);
        stage_->add_child(node);
        return node;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcOpening character ") + pack_path.c_str() + ": " + e.what());
        return nullptr;
    }
}

void DcOpening::load_cfg_scene(const std::string &cfg_path) {
    const dccore::Bytes raw = disc_->read(cfg_path);
    std::string text(raw.begin(), raw.end());
    env_ = Dictionary();
    Array lights;
    std::map<std::string, Vector3> spins;
    struct PendingModel { std::string path; Vector3 pos, rot; };
    std::vector<PendingModel> models;
    projection_ = 800.0f;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t cm = line.find("//");
        if (cm != std::string::npos) line.resize(cm);
        const std::vector<std::string> t = split_cfg(line);
        if (t.empty()) continue;
        const std::string &k = t[0];
        if (k == "AMBIENT") env_["ambient"] = Color(fnum(t, 1) / 128.0f, fnum(t, 2) / 128.0f, fnum(t, 3) / 128.0f);   // (128 = 1.0 on the PS2)
        else if (k == "BG_COL") env_["bg"] = Color(fnum(t, 1) / 255.0f, fnum(t, 2) / 255.0f, fnum(t, 3) / 255.0f);
        else if (k == "FOG") { env_["fog_near"] = fnum(t, 1); env_["fog_far"] = fnum(t, 2); env_["fog_color"] = Color(fnum(t, 3) / 255.0f, fnum(t, 4) / 255.0f, fnum(t, 5) / 255.0f); }
        else if (k == "PROJECTION") projection_ = fnum(t, 1);
        else if (k == "LIGHT_C" && (fnum(t, 1) != 0.0f || fnum(t, 2) != 0.0f || fnum(t, 3) != 0.0f)) {
            Dictionary d;
            d["direction"] = Vector3(fnum(t, 1), fnum(t, 2), fnum(t, 3)).normalized();
            d["color"] = Color(fnum(t, 4) / 128.0f, fnum(t, 5) / 128.0f, fnum(t, 6) / 128.0f);
            lights.push_back(d);
        } else if ((k == "GND" || k == "BLD" || k == "SKY") && t.size() >= 2) {
            models.push_back({t[1], Vector3(fnum(t, 2), fnum(t, 3), fnum(t, 4)) * 10.0f, Vector3(fnum(t, 5), fnum(t, 6), fnum(t, 7))});
        } else if (k == "OBJ_ROT" && t.size() >= 8) {
            spins[t[1]] = Vector3(fnum(t, 5), fnum(t, 6), fnum(t, 7));
        } else if (k == "FIRE") {
            OmniLight3D *l = memnew(OmniLight3D);
            l->set_position(Vector3(fnum(t, 1), fnum(t, 2), fnum(t, 3)) * 10.0f);
            l->set_param(Light3D::PARAM_RANGE, 120.0f);
            l->set_param(Light3D::PARAM_ENERGY, 1.4f);
            l->set_color(Color(1.0f, 0.65f, 0.3f));
            stage_->add_child(l);
            torches_.push_back(l);
        }
    }
    env_["lights"] = lights;
    for (const PendingModel &m : models) add_model(m.path, m.pos, m.rot, true, spins);
}

// Scene data: what each SCENE command builds (notes/opening.md sections 5, 6 and 10)
namespace {
struct CharSpec { int obj; const char *pack; const char *cfg; };
struct SceneSpec {
    const char *cfg = nullptr;                // lights / fog / fire
    std::vector<const char *> image_packs;    // the textures
    std::vector<const char *> model_packs;    // packs that hold models by bare name
    std::vector<std::pair<uint32_t, int>> tables;   // TITLE.BIN stage tables
    std::vector<const char *> models;         // single models at the origin
    std::vector<CharSpec> chars;
};
const SceneSpec &scene_spec(int n) {
    static const SceneSpec b1{"opdat/norn.cfg", {"opdat/norn/norn.pak"}, {}, {{0x01DD6B40u, 68}, {0x01DD73C0u, 23}}, {},
        {{8, "opdat/chara/03c01d.chr", ""}, {9, "opdat/chara/03p09a.chr", ""}, {10, "opdat/chara/03p10a.chr", ""}, {11, "opdat/chara/03c01d2.chr", ""}}};
    static const SceneSpec b2{"opdat/toan.cfg", {"opdat/toan/toan.pim"}, {}, {}, {"opdat/toan/t0101.mds"},
        {{8, "opdat/chara/03c01d.chr", ""}, {9, "opdat/chara/03p09a.chr", ""}, {10, "opdat/chara/03p10a.chr", ""}, {11, "opdat/chara/03c01d2.chr", ""}}};
    static const SceneSpec c3{"opdat/norn2.cfg", {"opdat/norn2/norn2.pim", "opdat/norn2/norn2b.pak", "opdat/norn2/ndance.pak"}, {}, {{0x01DD7F30u, 75}, {0x01DD8890u, 87}}, {},
        {{11, "opdat/norn2/norn2b.pak", "04c01d.cfg"}, {12, "opdat/norn2/norn2b.pak", "04p03a.cfg"}, {13, "opdat/norn2/norn2b.pak", "04p08a.cfg"}, {17, "opdat/norn2/norn2b.pak", "04p07a.cfg"},
         {18, "opdat/norn2/norn2b.pak", "04p02a.cfg"}, {14, "opdat/norn2/ndance.pak", "04p05b.cfg"}, {15, "opdat/norn2/ndance.pak", "04p01a.cfg"}, {16, "opdat/norn2/ndance.pak", "04p12a.cfg"},
         {19, "opdat/norn2/ndance.pak", "04p15a.cfg"}, {20, "opdat/norn2/ndance.pak", "04p16a.cfg"}}};
    static const SceneSpec c5{"opdat/norn2.cfg", {"opdat/norn2/majin.pak", "opdat/norn2/norn2.pim"}, {"opdat/norn2/majin.pak"}, {}, {"b0203.mds", "b0204.mds", "tukikage.mds"},
        {{1, "opdat/norn2/majin.pak", "04c08a.cfg"}, {3, "opdat/norn2/majin.pak", "04c09a.cfg"}}};
    static const SceneSpec c7{"opdat/norn2.cfg", {"opdat/norn3/norn3.pak", "opdat/norn2/norn2.pim", "opdat/norn2/majin.pak"}, {"opdat/norn3/norn3.pak"}, {{0x01DD9370u, 76}, {0x01DD9CF0u, 62}}, {},
        {{9, "opdat/norn3/norn3.pak", "04p09a.cfg"}, {11, "opdat/norn3/norn3.pak", "04c01d.cfg"}, {1, "opdat/norn2/majin.pak", "04c08a.cfg"}, {3, "opdat/norn2/majin.pak", "04c09a.cfg"}}};
    static const SceneSpec d8{"opdat/seirei.cfg", {"opdat/norn4/seirei.pak"}, {"opdat/norn4/seirei.pak"}, {}, {"b0401.mds", "b0402.mds"},
        {{8, "opdat/norn4/seirei.pak", "05c01d.cfg"}, {11, "opdat/norn4/seirei.pak", "05c01e.cfg"}, {21, "opdat/norn4/seirei.pak", "05c03c.cfg"}}};
    static const SceneSpec d9{"opdat/seirei.cfg", {"opdat/norn4/seirei2.pak", "opdat/norn4/seirei.pak"}, {"opdat/norn4/seirei2.pak"}, {{0x01DDAF00u, 10}}, {"b0403.mds"},
        {{8, "opdat/norn4/seirei.pak", "05c01d.cfg"}, {11, "opdat/norn4/seirei.pak", "05c01e.cfg"}, {22, "opdat/norn4/seirei2.pak", "buterfly.cfg"}}};
    static const SceneSpec empty{};
    switch (n) {
        case 1: return b1;
        case 2: return b2;
        case 3: case 4: case 6: return c3;
        case 5: return c5;
        case 7: return c7;
        case 8: return d8;
        case 9: return d9;
        default: return empty;
    }
}
}  // namespace

std::vector<uint8_t> DcOpening::read_model(const std::string &path) {
    if (path.find('/') != std::string::npos) return disc_->contains(path) ? disc_->read(path) : std::vector<uint8_t>();
    for (const auto &pk : model_packs_)
        if (const dccore::PackEntry *e = pk->find(path)) { dccore::View v = pk->data(*e); return std::vector<uint8_t>(v.begin(), v.end()); }
    return {};
}

void DcOpening::load_scene(int scene) {
    if (scene == loaded_scene_) return;
    clear_stage();
    loaded_scene_ = scene;
    materials_.clear();
    textures_.clear();
    model_packs_.clear();
    if (scene == 0) {
        add_images_from("opdat/dungeon/dungeon.pim");
        load_cfg_scene("opdat/opinfo.cfg");
        const char *const names[4] = {"01c07a.cfg", "01c08a.cfg", "01c11a.cfg", "01c09a.cfg"};
        const int slots[4] = {0, 1, 2, 3};
        for (int i = 0; i < 4; ++i) chars_[size_t(slots[i])] = make_character("opdat/dungeon/dungeon.pak", names[i]);
        // the dance: the leader and 35 dancers share one mesh each and play the phases back to back
        chars_[6] = make_character("opdat/chara/01p19a1a.chr", "01p19a1a.cfg", "01p19a.mds");
        for (int g = 0; g < 2; ++g) {   // the phases: {first frame, last frame, speed 0.5} per entry
            dance_range_[g].clear();
            const uint32_t base = g == 0 ? 0x01DD6140u : 0x01DD61E0u;
            for (int i = 0; i < 10; ++i) {
                int32_t v[2] = {0, 0};
                const size_t o = size_t(base - 0x01DABD00u) + size_t(i) * 16;
                if (o + 8 <= title_.size()) std::memcpy(v, title_.data() + o, 8);
                dance_range_[g].push_back({v[0], v[1]});
            }
            dance_phase_[g] = 0;
            dance_time_[g] = double(dance_range_[g][0][0]);
        }
        if (chars_[6]) {
            chars_[6]->set_position(Vector3(0, 0, 250));
            chars_[6]->set_rotation(Vector3(0, kPi, 0));
        }
        if (disc_->contains("opdat/chara/01p17a1a.chr")) {
            for (int k = 0; k < 35; ++k) {
                DcCharacter *d = make_character("opdat/chara/01p17a1a.chr", "01p17a1a.cfg", "01p17a.mds");
                if (!d) break;
                dancers_.push_back(d);
            }
            if (!dancers_.empty()) chars_[7] = dancers_[0];
            // the formation behind the leader until a camera clip moves them (InitDancerPos: table x10, z + 10)
            for (size_t k = 0; k < dancers_.size(); ++k) {
                const uint32_t a = 0x01DD6410u + uint32_t(k) * 16u - 0x01DABD00u;
                float v[3] = {0, 0, 0};
                if (a + 12 <= title_.size()) std::memcpy(v, title_.data() + a, 12);
                dancers_[k]->set_position(Vector3(v[0] * 10.0f, v[1] * 10.0f, v[2] * 10.0f + 10.0f));
                dancers_[k]->set_rotation(Vector3(0, kPi, 0));
            }
        }
    } else {
        const SceneSpec &sp = scene_spec(scene);
        for (const char *p : sp.image_packs) add_images_from(p);
        for (const char *p : sp.model_packs) {
            try { model_packs_.push_back(std::make_shared<dccore::Pack>(disc_->read(p))); } catch (const std::exception &) {}
        }
        if (sp.cfg) load_cfg_scene(sp.cfg);
        std::string prev;
        for (const auto &t : sp.tables)
            for (const dccore::OpStageEntry &e : dccore::opening_stage_table(title_, t.first, t.second)) {
                if (!e.model.empty()) prev = e.model;
                if (prev.empty()) continue;
                add_model(prev, Vector3(e.pos[0], e.pos[1], e.pos[2]), Vector3(e.rot_deg[0], e.rot_deg[1], e.rot_deg[2]), false, {});
            }
        for (const char *m : sp.models) add_model(m, Vector3(), Vector3(), false, {});
        for (const CharSpec &c : sp.chars) chars_[size_t(c.obj)] = make_character(c.pack, c.cfg);
    }
    apply_state();
}

static const char *const kPhaseFiles[10] = {"1a", "1b", "2a", "2a", "2b", "3", "3b", "4a", "4b", "5"};

void DcOpening::load_dance_phase(int group, int phase) {
    DcCharacter *c = group == 0 ? chars_[6] : (dancers_.empty() ? nullptr : dancers_[0]);
    if (!c) return;
    const std::string stem = std::string(group == 0 ? "01p19a" : "01p17a") + kPhaseFiles[std::clamp(phase, 0, 9)];
    const std::string path = "opdat/chara/" + stem + ".chr";
    if (!disc_->contains(path)) return;
    try {
        dccore::Pack pk(disc_->read(path));
        if (const dccore::PackEntry *e = pk.find(stem + ".mot")) c->get_character()->tracks = dccore::parse_mot(pk.data(*e));
    } catch (const std::exception &) {
    }
}

// the 10 phases of the dance run back to back whatever the script does; the camera only looks at them
void DcOpening::step_dance() {
    for (int g = 0; g < 2; ++g) {
        DcCharacter *c = g == 0 ? chars_[6] : (dancers_.empty() ? nullptr : dancers_[0]);
        if (!c || dance_range_[g].empty()) continue;
        dance_time_[g] += 0.5;
        const auto &r = dance_range_[g][size_t(dance_phase_[g])];
        if (dance_time_[g] >= double(r[1]) - 1.0) {
            dance_phase_[g] = std::min(dance_phase_[g] + 1, 9);
            load_dance_phase(g, dance_phase_[g]);
            dance_time_[g] = double(dance_range_[g][size_t(dance_phase_[g])][0]);
        }
    }
}

// the clocks of the stage objects' motions advance every step ...
void DcOpening::step_objects() {
    const dccore::OpeningSim &s = *sim_;
    for (size_t i = 0; i < 23; ++i) {
        DcCharacter *c = chars_[i];
        if (!c || i == 6 || i == 7) continue;
        const dccore::OpObject &o = s.obj[i];
        if (!o.disp) continue;
        const auto &motions = c->get_character()->motions;
        if (obj_motion_shown_[i] != o.motion_serial * 64 + o.motion) {   // a new motion command
            obj_motion_shown_[i] = o.motion_serial * 64 + o.motion;
            if (size_t(o.motion) < motions.size()) obj_time_[i] = motions[size_t(o.motion)].start;
        } else if (size_t(o.motion) < motions.size()) {
            const dccore::MotionEntry &m = motions[size_t(o.motion)];
            obj_time_[i] += m.speed;
            if (std::floor(obj_time_[i]) >= m.end) {
                if (o.motion2 >= 0 && size_t(o.motion2) < motions.size()) obj_time_[i] = motions[size_t(o.motion2)].start;
                else obj_time_[i] -= (m.end - m.start);
            }
        }
    }
}

// ... the poses are only worked out for the picture
void DcOpening::apply_objects() {
    const dccore::OpeningSim &s = *sim_;
    for (size_t i = 0; i < 23; ++i) {
        DcCharacter *c = chars_[i];
        if (!c) continue;
        const bool show = s.obj[i].disp;
        c->set_visible(show);
        if (i == 7) for (DcCharacter *d : dancers_) d->set_visible(show);
        if (!show) continue;
        if (i == 6 || i == 7) {
            const double t = dance_time_[i == 6 ? 0 : 1];
            c->set_time(t);
            if (i == 7) for (DcCharacter *d : dancers_) d->set_time(t);
        } else {
            c->set_time(double(obj_time_[i]));
        }
    }
}

void DcOpening::apply_state() {
    const dccore::OpeningSim &s = *sim_;
    // the camera clip of the cut
    if (!s.clip_name().empty()) {
        auto it = clips_.find(s.clip_name());
        if (it == clips_.end()) {
            std::shared_ptr<dccore::SneClip> clip;
            try {
                clip = std::make_shared<dccore::SneClip>(dccore::load_sne(disc_->read(s.clip_name()), s.clip_name()));
            } catch (const std::exception &) {
            }
            it = clips_.emplace(s.clip_name(), clip).first;
        }
        clip_ = it->second;
    }
    if (clip_) {
        float pos[3], ref[3], fov = 0;
        if (clip_->camera_at(double(s.cam_time), pos, ref, &fov)) {
            Transform3D t;
            t.origin = Vector3(pos[0], pos[1], pos[2]);
            if (t.origin.distance_squared_to(Vector3(ref[0], ref[1], ref[2])) > 1e-6f) camera_->set_transform(t.looking_at(Vector3(ref[0], ref[1], ref[2]), Vector3(0, 1, 0)));
            const double proj = fov > 0.5f ? 240.0 / std::tan(double(fov) * kPi / 360.0) : double(projection_);
            camera_->set_fov(2.0 * std::atan(224.0 / proj) * 180.0 / kPi);
            const int key = s.cam_key;
            float near_plane = 6.0f;
            if (key == 48 || key == 42 || key == 41 || key == 40) near_plane = 18.0f;
            else if (key == 39 || key == 0) near_plane = 16.0f;
            else if (key == 37 || key == 15 || key == 3 || key == 2) near_plane = 10.0f;
            camera_->set_near(near_plane);
        }
        // the actors stand where the clip's nodes put them
        if (s.scene != 0) {
            static const std::map<int, const char *> nodes_b = {{8, "c01d"}, {9, "p09a"}, {10, "p10a"}, {11, "c01d"}};
            static const std::map<int, const char *> nodes_c = {{1, "c08a"}, {3, "c09a"}, {4, "c08a"}, {5, "point"}, {9, "p09a"}, {11, "c01d"}, {12, "p03a"}, {13, "p08a"}, {14, "p05b"},
                {15, "p01a"}, {16, "p12a"}, {17, "p07a"}, {18, "p02a"}, {19, "p15a"}, {20, "p16a"}};
            static const std::map<int, const char *> nodes_d = {{8, "c01d"}, {11, "c01d"}, {21, "c03c"}, {22, "tyou"}, {5, "tyou2"}};
            const std::map<int, const char *> &table = s.scene <= 2 ? nodes_b : (s.scene <= 7 ? nodes_c : nodes_d);
            for (const auto &kv : table) {
                DcCharacter *c = chars_[size_t(kv.first)];
                if (!c || !s.obj[size_t(kv.first)].disp) continue;
                float p[3], yaw;
                if (clip_->node_at(kv.second, double(s.cam_time), p, &yaw)) {
                    c->set_position(Vector3(p[0], p[1], p[2]));
                    c->set_rotation(Vector3(0, yaw, 0));
                }
            }
        }
        if (s.scene == 0) {
            for (int i = 0; i < 8; ++i) {
                DcCharacter *c = chars_[size_t(i)];
                if (!c || !s.obj[size_t(i)].disp) continue;
                if (i == 7) {
                    for (size_t k = 0; k < dancers_.size(); ++k) {
                        float p[3], yaw;
                        if (clip_->node_at("p17a" + std::to_string(k + 1), double(s.cam_time), p, &yaw)) {
                            dancers_[k]->set_position(Vector3(p[0], p[1], p[2]));
                            dancers_[k]->set_rotation(Vector3(0, yaw, 0));
                        }
                    }
                    continue;
                }
                float p[3], yaw;
                if (clip_->node_at(kNodeA[i], double(s.cam_time), p, &yaw)) {
                    c->set_position(Vector3(p[0], p[1], p[2]));
                    c->set_rotation(Vector3(0, yaw, 0));
                }
            }
        }
    }
}

void DcOpening::step_ticks(int n) {
    int done = 0;
    for (int i = 0; i < n; ++i) {
        sim_->step();
        ++done;
        for (const dccore::OpEvent &e : sim_->events) {
            if (e.kind == dccore::OpEvent::Scene) load_scene(int(e.a[0]));
            if (e.kind == dccore::OpEvent::Bom) spawn_bomb(Vector3(e.a[0], e.a[1], e.a[2]) * 10.0f, e.a[3] * 10.0f);
            if (e.kind == dccore::OpEvent::Beem) spawn_beam(Vector3(e.a[0], e.a[1], e.a[2]) * 10.0f, Vector3(e.a[3], e.a[4], e.a[5]) * 10.0f, e.a[0] < 0.0f);
            Dictionary d;
            d["kind"] = int(e.kind);
            Array a;
            for (float v : e.a) a.push_back(v);
            d["a"] = a;
            events_.push_back(d);
        }
        step_dance();
        step_objects();
        step_effects();
        if (sim_->finished()) break;
    }
    apply_state();
    apply_objects();
    for (Spin &sp : spins_) sp.node->rotate_y(sp.axis_deg_per_step.y * kPi / 180.0f * float(done));
}

void DcOpening::fast_forward(int steps) {
    const bool was = auto_run_;
    auto_run_ = false;
    step_ticks(steps);
    auto_run_ = was;
}

void DcOpening::_physics_process(double delta) {
    if (!auto_run_ || !sim_) return;
    accumulator_ += delta * 60.0;
    int n = int(accumulator_);
    accumulator_ -= n;
    if (n > 4) n = 4;
    if (n > 0) step_ticks(n);
}

Dictionary DcOpening::get_ui() const {
    Dictionary d;
    if (!sim_) return d;
    const dccore::OpeningSim &s = *sim_;
    d["fade_alpha"] = s.fade_alpha / 128.0f;
    d["fade_white"] = s.fade_white;
    d["wait_key"] = s.waiting_key();
    d["finished"] = s.finished();
    d["steps"] = int(s.steps);
    d["cut"] = s.cut;
    d["scene"] = s.scene;
    if (s.balloon.active) {
        Dictionary b;
        b["msg"] = s.balloon.msg;
        b["text"] = get_message(s.balloon.msg);
        b["x"] = s.balloon.x; b["y"] = s.balloon.y; b["tx"] = s.balloon.tx; b["ty"] = s.balloon.ty;
        b["speaker"] = s.balloon.speaker;
        d["balloon"] = b;
    }
    d["sprite"] = s.sprite_on ? s.sprite_id : 0;
    return d;
}

Dictionary DcOpening::get_debug() const {
    Dictionary d;
    d["dancers"] = int(dancers_.size());
    if (!dancers_.empty()) { d["d0_pos"] = dancers_[0]->get_position(); d["d0_visible"] = dancers_[0]->is_visible(); d["d0_time"] = dancers_[0]->get_time(); }
    d["clip"] = String(sim_->clip_name().c_str());
    d["clip_ok"] = bool(clip_);
    for (size_t i = 0; i < 8; ++i) d[String("char") + String::num_int64(int64_t(i))] = chars_[i] != nullptr;
    return d;
}

Dictionary DcOpening::get_music_pack(int scene) const {
    static const struct { int scene; const char *path, *stem; } table[] = {
        {0, "opdat/dungeon/dungeon.pak", "o01a"}, {1, "opdat/norn/norn.pak", "o02a"}, {3, "opdat/norn2/norn2b.pak", "o03a"},
        {5, "opdat/norn2/majin.pak", "o03b"}, {8, "opdat/norn4/seirei.pak", "o04a"}, {9, "opdat/norn4/seirei2.pak", "o04b"},
    };
    Dictionary d;
    for (const auto &t : table) {
        if (t.scene != scene || !disc_->contains(t.path)) continue;
        const dccore::Bytes b = disc_->read(t.path);
        PackedByteArray arr;
        arr.resize(int64_t(b.size()));
        std::memcpy(arr.ptrw(), b.data(), b.size());
        d["data"] = arr;
        d["stem"] = String(t.stem);
    }
    return d;
}

// simple stand-ins for the opening's explosions (BOM) and Majin's beams (BEEM): additive shapes that grow and fade
void DcOpening::spawn_bomb(const Vector3 &pos, float size) {
    MeshInstance3D *mi = memnew(MeshInstance3D);
    Ref<SphereMesh> sm;
    sm.instantiate();
    sm->set_radius(1.0f);
    sm->set_height(2.0f);
    mi->set_mesh(sm);
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
    m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    m->set_albedo(Color(1.0f, 0.55f, 0.15f, 0.9f));
    mi->set_material_override(m);
    mi->set_position(pos);
    mi->set_scale(Vector3(1, 1, 1) * 0.1f);
    stage_->add_child(mi);
    effects_.push_back({mi, 0, 30, size > 0.0f ? size : 60.0f, false});
}

void DcOpening::spawn_beam(const Vector3 &from, const Vector3 &to, bool flash_only) {
    if (flash_only) return;
    MeshInstance3D *mi = memnew(MeshInstance3D);
    Ref<BoxMesh> bm;
    bm.instantiate();
    bm->set_size(Vector3(4.0f, 4.0f, 1.0f));
    mi->set_mesh(bm);
    Ref<StandardMaterial3D> m;
    m.instantiate();
    m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
    m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
    m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
    m->set_albedo(Color(1.0f, 0.95f, 0.6f, 0.9f));
    mi->set_material_override(m);
    stage_->add_child(mi);
    const Vector3 mid = (from + to) * 0.5f;
    const float len = from.distance_to(to);
    Transform3D t;
    t.origin = mid;
    if (len > 1e-3f) t = t.looking_at(to, Vector3(0, 1, 0));
    t.basis = t.basis.scaled(Vector3(1, 1, len));
    mi->set_transform(t);
    effects_.push_back({mi, 0, 14, 0.0f, true});
}

void DcOpening::step_effects() {
    for (size_t i = 0; i < effects_.size();) {
        Effect &e = effects_[i];
        ++e.age;
        const float f = float(e.age) / float(e.life);
        if (!e.beam) e.node->set_scale(Vector3(1, 1, 1) * (0.1f + e.size * f));
        if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(e.node)) {
            Ref<StandardMaterial3D> m = mi->get_material_override();
            if (m.is_valid()) { Color c = m->get_albedo(); c.a = std::max(0.0f, 0.9f * (1.0f - f)); m->set_albedo(c); }
        }
        if (e.age >= e.life) {
            stage_->remove_child(e.node);
            e.node->queue_free();
            effects_.erase(effects_.begin() + long(i));
        } else {
            ++i;
        }
    }
}
