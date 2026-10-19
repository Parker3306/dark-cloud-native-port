#include "dc_dungeon.h"
#include "dc_character.h"

#include <cmath>
#include <map>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

namespace {

Ref<ImageTexture> to_texture(const dccore::Image &im) {
    PackedByteArray data;
    data.resize(int64_t(im.rgba.size()));
    std::memcpy(data.ptrw(), im.rgba.data(), im.rgba.size());
    Ref<Image> img = Image::create_from_data(int(im.width), int(im.height), false, Image::FORMAT_RGBA8, data);
    img->generate_mipmaps();
    return ImageTexture::create_from_image(img);
}

} // namespace

void DcDungeon::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_info"), &DcDungeon::get_info);
    ClassDB::bind_method(D_METHOD("get_start_position"), &DcDungeon::get_start_position);
    ClassDB::bind_method(D_METHOD("get_center"), &DcDungeon::get_center);
    ClassDB::bind_method(D_METHOD("get_room_count"), &DcDungeon::get_room_count);
    ClassDB::bind_method(D_METHOD("get_environment"), &DcDungeon::get_environment);
    ClassDB::bind_method(D_METHOD("open_door", "x", "y", "kind"), &DcDungeon::open_door);
    ClassDB::bind_method(D_METHOD("set_object", "name", "offset", "rotation"), &DcDungeon::set_object);
    ClassDB::bind_method(D_METHOD("set_object_visible", "name", "visible"), &DcDungeon::set_object_visible);
}

Vector3 DcDungeon::get_start_position() const {
    if (layout_.fixed && dungeon_) {
        const auto st = dccore::fixed_start(*dungeon_);
        return Vector3(st[0], st[1], st[2]);
    }
    for (int y = 0; y < dccore::kGrid; ++y)
        for (int x = 0; x < dccore::kGrid; ++x)
            if (layout_.at(x, y).flags & 0x200) return Vector3(x * dccore::kCellSize, 0, y * dccore::kCellSize);
    return Vector3();
}

void DcDungeon::set_object(const String &name, const Vector3 &offset, const Vector3 &rotation) {
    auto it = objects_.find(name.utf8().get_data());
    if (it == objects_.end()) return;
    const Basis turn = Basis::from_euler(Vector3(-rotation.x, -rotation.y, -rotation.z));   // (the game's angles run the other way round: a cell's quarter turns are -90 degrees in Godot)
    for (DynObject &o : it->second) {
        const Vector3 origin = o.pivot + offset - turn.xform(o.pivot);   // turned about its own frame, then moved
        const Transform3D local(turn, origin);
        o.node->set_transform(o.top ? o.cell * local : local);
    }
}

void DcDungeon::set_object_visible(const String &name, bool visible) {
    auto it = objects_.find(name.utf8().get_data());
    if (it == objects_.end()) return;
    for (DynObject &o : it->second) o.node->set_visible(visible);
}

void DcDungeon::open_door(int x, int y, int kind) {
    auto it = door_nodes_.find(y * dccore::kGrid + x);
    if (it == door_nodes_.end()) return;
    for (auto &d : it->second)
        if (d.first & kind) d.second->set_visible(false);
}

Vector3 DcDungeon::get_center() const { return Vector3(8 * dccore::kCellSize, 0, 8 * dccore::kCellSize); }

Dictionary DcDungeon::get_environment() const {
    Dictionary d;
    if (!dungeon_) return d;
    const dccore::CfgEnv &e = back_ ? dungeon_->cfg.back : dungeon_->cfg.front;
    d["ambient"] = Color(e.ambient[0] / 255.0f, e.ambient[1] / 255.0f, e.ambient[2] / 255.0f);
    d["fog_near"] = e.fog_near;
    d["fog_far"] = e.fog_far;
    d["fog_color"] = Color(e.fog_color[0] / 255.0f, e.fog_color[1] / 255.0f, e.fog_color[2] / 255.0f);
    d["background"] = Color(e.bg[0] / 255.0f, e.bg[1] / 255.0f, e.bg[2] / 255.0f);
    Array lights;
    for (const auto &l : e.lights) {
        Dictionary ld;
        ld["direction"] = Vector3(l.dir[0], l.dir[1], l.dir[2]);
        ld["color"] = Color(l.color[0] / 255.0f, l.color[1] / 255.0f, l.color[2] / 255.0f);
        lights.push_back(ld);
    }
    d["lights"] = lights;
    d["view_level"] = dungeon_->cfg.view_level;
    return d;
}

void DcDungeon::setup(std::shared_ptr<dccore::Dungeon> dungeon, const dccore::FloorLayout &layout) {
    dungeon_ = std::move(dungeon);
    layout_ = layout;

    std::map<std::string, Ref<StandardMaterial3D>> materials;
    auto material_for = [&](const std::string &tex) -> Ref<StandardMaterial3D> {
        auto it = materials.find(tex);
        if (it != materials.end()) return it->second;
        Ref<StandardMaterial3D> m;
        m.instantiate();
        m->set_roughness(1.0f);
        m->set_metallic(0.0f);
        m->set_cull_mode(BaseMaterial3D::CULL_BACK);
        auto t = dungeon_->textures.find(tex);
        if (t != dungeon_->textures.end()) {
            m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, to_texture(t->second));
            m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
            m->set_alpha_scissor_threshold(0.5f);
            m->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
        } else {
            m->set_albedo(Color(0.8f, 0.2f, 0.8f));
        }
        materials[tex] = m;
        return m;
    };

    std::map<std::string, Ref<ArrayMesh>> meshes;
    int64_t missing_texture_surfaces = 0, triangles = 0;
    auto base_of = [](const std::string &n) { const size_t us = n.find("__"); return us == std::string::npos ? n : n.substr(0, us); };   // (object names carry draw flags after a double underscore)
    // the objects of a surface's chain (outermost first) that a script addresses
    auto dyn_chain = [&](const dccore::Surface &s) {
        std::vector<std::string> out;
        if (dyn_names_.empty()) return out;
        size_t from = 0;
        while (from <= s.path.size()) {
            size_t bar = s.path.find('|', from);
            if (bar == std::string::npos) bar = s.path.size();
            const std::string b = base_of(s.path.substr(from, bar - from));
            if (dyn_names_.count(b)) out.push_back(b);
            from = bar + 1;
        }
        return out;
    };
    // the innermost one owns the surface ("" when the surface belongs to the static part)
    auto dyn_root = [&](const dccore::Surface &s) -> std::string {
        const std::vector<std::string> c = dyn_chain(s);
        return c.empty() ? std::string() : c.back();
    };
    // `dyn`: "" builds the part of the model that stays; a name builds the object of that name (with its children)
    auto mesh_for = [&](const std::string &file, int door_kind, const std::string &dyn = std::string()) -> Ref<ArrayMesh> {
        const std::string key = (door_kind ? file + "#door" + std::to_string(door_kind) : file) + (dyn.empty() ? "" : "#obj:" + dyn);
        auto it = meshes.find(key);
        if (it != meshes.end()) return it->second;
        Ref<ArrayMesh> mesh;
        auto mi = dungeon_->models.find(file);
        if (mi != dungeon_->models.end()) {
            mesh.instantiate();
            static const std::vector<dccore::Surface> kNone;
            const auto door_it = door_kind ? mi->second.door_surfaces.find(door_kind) : mi->second.door_surfaces.end();
            for (const dccore::Surface &s : door_kind ? (door_it != mi->second.door_surfaces.end() ? door_it->second : kNone) : mi->second.surfaces) {
                if (s.indices.empty()) continue;
                if (!door_kind) {
                    const std::string root = dyn_root(s);
                    if (dyn.empty() ? !root.empty() : root != dyn) continue;
                }
                PackedVector3Array v, n;
                PackedVector2Array uv;
                PackedInt32Array idx;
                const int64_t nv = int64_t(s.positions.size() / 3);
                v.resize(nv); n.resize(nv); uv.resize(nv);
                for (int64_t i = 0; i < nv; ++i) {
                    v.set(i, Vector3(s.positions[3 * i], s.positions[3 * i + 1], s.positions[3 * i + 2]));
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
                mesh->surface_set_material(mesh->get_surface_count() - 1, material_for(s.texture));
                if (!s.texture.empty() && dungeon_->textures.find(s.texture) == dungeon_->textures.end()) ++missing_texture_surfaces;
                triangles += int64_t(s.indices.size() / 3);
            }
        }
        meshes[key] = mesh;
        return mesh;
    };

    int64_t placed = 0, cells = 0, unknown_parts = 0, missing_models = 0;
    const int cell_count = layout_.fixed ? int(dungeon_->cfg.parts.size()) : dccore::kGrid * dccore::kGrid;
    for (int ci = 0; ci < cell_count; ++ci) {
        {
            const int x = layout_.fixed ? 0 : ci % dccore::kGrid, y = layout_.fixed ? 0 : ci / dccore::kGrid;
            dccore::MapCell fixed_cell;
            if (layout_.fixed) fixed_cell.part = ci;
            const dccore::MapCell &c = layout_.fixed ? fixed_cell : layout_.at(x, y);
            if (c.part < 0) continue;
            ++cells;
            if (size_t(c.part) >= dungeon_->cfg.parts.size()) { ++unknown_parts; continue; }
            for (const dccore::PartModelRef &m : dungeon_->cfg.parts[size_t(c.part)].models) {
                Ref<ArrayMesh> mesh = mesh_for(m.file, 0);
                bool has_doors = false;
                const int turn = dccore::model_turn_index(c.rot, m.rot);
                const Basis basis(Vector3(0, 1, 0), -real_t(turn) * real_t(3.14159265358979323846) * real_t(0.5));
                const Vector3 at(x * dccore::kCellSize + m.offset[0], m.offset[1], y * dccore::kCellSize + m.offset[2]);
                auto mi_model = dungeon_->models.find(m.file);
                if (mi_model != dungeon_->models.end())
                    for (const auto &kv : mi_model->second.door_surfaces) {
                        Ref<ArrayMesh> door_mesh = mesh_for(m.file, kv.first);
                        if (door_mesh.is_null() || door_mesh->get_surface_count() == 0) continue;
                        MeshInstance3D *dm = memnew(MeshInstance3D);
                        dm->set_mesh(door_mesh);
                        dm->set_basis(basis);
                        dm->set_position(at);
                        add_child(dm);
                        door_nodes_[y * dccore::kGrid + x].push_back({kv.first, dm});
                        has_doors = true;
                    }
                if (!dyn_names_.empty() && mi_model != dungeon_->models.end()) {   // the objects a script moves get nodes of their own (nested like the model's objects)
                    std::map<std::string, std::string> parent;   // dynamic object -> the dynamic object it hangs under ("" none)
                    std::vector<std::string> order;
                    for (const dccore::Surface &s : mi_model->second.surfaces) {
                        const std::vector<std::string> chain = dyn_chain(s);
                        for (size_t k = 0; k < chain.size(); ++k)
                            if (!parent.count(chain[k])) { parent[chain[k]] = k ? chain[k - 1] : std::string(); order.push_back(chain[k]); }
                    }
                    auto frames = dungeon_->marker_frames.find(m.file);
                    std::map<std::string, Node3D *> made;
                    const Transform3D cell(basis, at);
                    for (const std::string &r : order) {
                        Ref<ArrayMesh> om = mesh_for(m.file, 0, r);
                        Node3D *on;
                        if (om.is_valid() && om->get_surface_count() > 0) {
                            MeshInstance3D *omi = memnew(MeshInstance3D);
                            omi->set_mesh(om);
                            omi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
                            on = omi;
                        } else {
                            on = memnew(Node3D);
                        }
                        Vector3 pivot;
                        if (frames != dungeon_->marker_frames.end())
                            for (const auto &fr : frames->second)
                                if (base_of(fr.first) == r) { pivot = Vector3(fr.second[0], fr.second[1], fr.second[2]); break; }
                        const std::string &par = parent[r];
                        const bool top = par.empty() || !made.count(par);
                        if (top) { on->set_transform(cell); add_child(on); }
                        else made[par]->add_child(on);
                        made[r] = on;
                        objects_[r].push_back({on, cell, pivot, top});
                    }
                }
                if (mesh.is_null() || mesh->get_surface_count() == 0) { if (!has_doors) ++missing_models; continue; }
                MeshInstance3D *mi = memnew(MeshInstance3D);
                mi->set_mesh(mesh);
                mi->set_basis(basis);
                mi->set_position(at);
                mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);   // (the rock does not shadow the floor: only the characters do)
                add_child(mi);
                ++placed;
            }
        }
    }
    // Dran's arena: twelve tiles, shown whole (the crumbling motion is not played yet)
    for (const auto &tile : dungeon_->drans) {
        for (const dccore::Part &part : tile->parts)   // the tiles use the floor's own texture sheet
            for (const dccore::Surface &sf : part.surfaces) {
                auto tx = dungeon_->textures.find(sf.texture);
                if (!tile->textures.count(sf.texture) && tx != dungeon_->textures.end()) tile->textures[sf.texture] = tx->second;
            }
        DcCharacter *t = memnew(DcCharacter);
        Dictionary tinfo;
        t->setup(tile, tinfo);
        t->set_playing(false);
        add_child(t);
    }
    // the torches of the parts: a warm light with a little flicker (the game only shows the flame sprite; the light is the modern extra)
    for (const auto &f : dccore::floor_fires(*dungeon_, layout_)) {
        OmniLight3D *l = memnew(OmniLight3D);
        l->set_position(Vector3(f[0], f[1], f[2]));
        l->set_color(Color(1.0f, 0.62f, 0.28f));
        l->set_param(Light3D::PARAM_RANGE, 90.0f);
        l->set_param(Light3D::PARAM_ENERGY, 2.2f);
        l->set_param(Light3D::PARAM_ATTENUATION, 1.4f);
        l->set_shadow(false);
        l->add_to_group("torch");
        add_child(l);
    }
    info_["cells"] = cells;
    info_["models_placed"] = placed;
    info_["unknown_parts"] = unknown_parts;
    info_["missing_models"] = missing_models;
    info_["triangles_unique"] = triangles;
    info_["textures"] = int64_t(dungeon_->textures.size());
    info_["missing_texture_surfaces"] = missing_texture_surfaces;
    info_["failed_models"] = int64_t(dungeon_->failed_models.size());
    info_["rooms"] = int64_t(layout_.rooms.size());
    info_["seed"] = int64_t(layout_.seed);
}
