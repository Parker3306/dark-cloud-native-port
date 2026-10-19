#include "dc_town.h"
#include <map>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <algorithm>
#include <cstring>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/geometry_instance3d.hpp>
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

Ref<ArrayMesh> mesh_of(const dccore::StaticModel &m, const std::function<Ref<StandardMaterial3D>(const std::string &)> &material_for) {
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    // surfaces that share a material are one surface (a mesh holds at most 256; some story maps have more objects than that)
    std::vector<std::string> order;
    std::map<std::string, std::vector<const dccore::Surface *>> groups;
    for (const dccore::Surface &s : m.surfaces) {
        if (s.indices.empty()) continue;
        const std::string key = s.additive ? s.texture + "#add" : s.texture;
        if (!groups.count(key)) order.push_back(key);
        groups[key].push_back(&s);
    }
    for (const std::string &key : order) {
        PackedVector3Array v, n;
        PackedVector2Array uv;
        PackedInt32Array idx;
        for (const dccore::Surface *sp : groups[key]) {
            const dccore::Surface &s = *sp;
            const int64_t base = v.size();
            const int64_t nv = int64_t(s.positions.size() / 3);
            for (int64_t i = 0; i < nv; ++i) {
                v.push_back(Vector3(s.positions[3 * i], s.positions[3 * i + 1], s.positions[3 * i + 2]));
                n.push_back(Vector3(s.normals[3 * i], s.normals[3 * i + 1], s.normals[3 * i + 2]));
                uv.push_back(Vector2(s.uvs[2 * i], s.uvs[2 * i + 1]));
            }
            for (size_t i = 0; i < s.indices.size(); ++i) idx.push_back(int32_t(base + int64_t(s.indices[i])));
        }
        Array arrays;
        arrays.resize(Mesh::ARRAY_MAX);
        arrays[Mesh::ARRAY_VERTEX] = v;
        arrays[Mesh::ARRAY_NORMAL] = n;
        arrays[Mesh::ARRAY_TEX_UV] = uv;
        arrays[Mesh::ARRAY_INDEX] = idx;
        mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
        mesh->surface_set_material(mesh->get_surface_count() - 1, material_for(key));
    }
    return mesh;
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

}  // namespace

float DcTown::frand() {
    rng_state_ = rng_state_ * 1664525u + 1013904223u;
    return float(rng_state_ >> 8) / float(1 << 24);
}

void DcTown::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_info"), &DcTown::get_info);
    ClassDB::bind_method(D_METHOD("get_light", "t"), &DcTown::get_light);
    ClassDB::bind_method(D_METHOD("update_sky", "t", "camera_pos"), &DcTown::update_sky);
    ClassDB::bind_method(D_METHOD("get_events"), &DcTown::get_events);
    ClassDB::bind_method(D_METHOD("get_people"), &DcTown::get_people);
    ClassDB::bind_method(D_METHOD("set_clock", "t"), &DcTown::set_clock);
    ClassDB::bind_method(D_METHOD("set_instance_offset", "index", "dy"), &DcTown::set_instance_offset);
    ClassDB::bind_method(D_METHOD("set_frame_visible", "instance", "frame", "visible"), &DcTown::set_frame_visible);
    ClassDB::bind_method(D_METHOD("set_frame_transform", "instance", "frame", "pos", "rot", "set_pos", "set_rot"), &DcTown::set_frame_transform, DEFVAL(true), DEFVAL(true));
}

void DcTown::setup(std::shared_ptr<dccore::Town> town) {
    town_ = std::move(town);
    auto materials_ptr = std::make_shared<std::map<std::string, Ref<StandardMaterial3D>>>();
    auto material_for = [this, materials_ptr](const std::string &tex) -> Ref<StandardMaterial3D> {
        auto &materials = *materials_ptr;
        auto it = materials.find(tex);
        if (it != materials.end()) return it->second;
        Ref<StandardMaterial3D> m;
        m.instantiate();
        m->set_roughness(1.0f);
        m->set_metallic(0.0f);
        m->set_cull_mode(BaseMaterial3D::CULL_BACK);
        const bool additive = tex.size() > 4 && tex.compare(tex.size() - 4, 4, "#add") == 0;
        auto t = town_->textures.find(additive ? tex.substr(0, tex.size() - 4) : tex);
        if (additive) {
            m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
            m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
            m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
            m->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, false);
            m->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
            if (t != town_->textures.end()) m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, to_texture(t->second));
        } else if (t != town_->textures.end()) {
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
    std::vector<Ref<ArrayMesh>> meshes(town_->assets.size());
    for (size_t i = 0; i < town_->assets.size(); ++i) meshes[i] = mesh_of(town_->assets[i].lod[0], material_for);
    inst_frames_.assign(town_->instances.size(), {});
    instance_nodes_.assign(town_->instances.size(), nullptr);
    inst_extra_.assign(town_->instances.size(), {});
    asset_meshes_ = meshes;
    material_for_ = material_for;
    build_instance_ = [this, material_for](size_t ii) {
        const dccore::TownInstance &in = town_->instances[ii];
        const dccore::TownAsset &asset = town_->assets[size_t(in.asset)];
        Ref<ArrayMesh> mesh = asset_mesh(in.asset);
        if (mesh.is_null() || mesh->get_surface_count() == 0) return;
        Node3D *root = memnew(Node3D);
        root->set_basis(Basis(Vector3(0, 1, 0), in.yaw));
        root->set_position(Vector3(in.pos[0], in.pos[1], in.pos[2]));
        add_child(root);
        instance_nodes_[ii] = root;
        // frames the scripts or the object animations move: the function records, the cfg's OBJ_ANIME of the owner
        std::set<std::string> dynamic = inst_extra_[ii];   // frames the scripts asked for later
        std::vector<const dccore::MapAnim *> owner_anims;
        for (const dccore::PtsFunc &fn : asset.info.funcs)
            if (fn.type == dccore::kFnObjAnime || fn.type == dccore::kFnTimer) dynamic.insert(dccore::frame_key(fn.target));
        const dccore::MapOwned *owner = nullptr;
        if (in.owner_object >= 0) owner = &town_->info.objects[size_t(in.owner_object)];
        else if (in.part_def >= 0) { auto pit = town_->info.parts.find(in.part_def); if (pit != town_->info.parts.end()) owner = &pit->second; }
        if (owner)
            for (int ai : owner->anims) {
                owner_anims.push_back(&town_->info.anims[size_t(ai - 1)]);
                dynamic.insert(dccore::frame_key(town_->info.anims[size_t(ai - 1)].target));
            }
        if (dynamic.empty()) {
            MeshInstance3D *mi = memnew(MeshInstance3D);
            mi->set_mesh(mesh);
            root->add_child(mi);
            return;
        }
        std::vector<const dccore::Surface *> rest;
        std::map<std::string, std::vector<const dccore::Surface *>> by_frame;
        for (const dccore::Surface &s : asset.lod[0].surfaces) {
            const std::string key = dccore::frame_key(s.object);
            if (dynamic.count(key)) by_frame[key].push_back(&s); else rest.push_back(&s);
        }
        if (!rest.empty()) {
            MeshInstance3D *mi = memnew(MeshInstance3D);
            mi->set_mesh(mesh_of_surfaces(rest, Vector3(), material_for));
            root->add_child(mi);
        }
        for (auto &kv : by_frame) {
            // the frames of one key share the pivot of the first surface
            const dccore::Surface &first = *kv.second.front();
            const Vector3 pivot(first.pivot[0], first.pivot[1], first.pivot[2]);
            Node3D *fr = memnew(Node3D);
            fr->set_position(pivot);
            MeshInstance3D *mi = memnew(MeshInstance3D);
            mi->set_mesh(mesh_of_surfaces(kv.second, pivot, material_for));
            fr->add_child(mi);
            root->add_child(fr);
            inst_frames_[ii][kv.first].push_back({fr, pivot});
        }
        // object animations and timers
        auto add_anim = [&](const std::string &target, int kind, int mode, int steps, const float *init, const float *delta, const float *end) {
            auto it = inst_frames_[ii].find(dccore::frame_key(target));
            if (it == inst_frames_[ii].end()) return;
            for (const FrameRef &fr : it->second) {
                Anim a;
                a.node = fr.node;
                a.pivot = fr.pivot;
                a.kind = kind; a.mode = mode; a.steps = steps;
                for (int k = 0; k < 3; ++k) { a.init[k] = init[k]; a.delta[k] = delta[k]; a.end[k] = end[k]; a.cur[k] = init[k]; }
                if (steps > 0)
                    for (int k = 0; k < 3; ++k) a.delta[k] = (end[k] - init[k]) / float(steps);
                a.instance = int(ii);
                anims_.push_back(a);
            }
        };
        for (const dccore::PtsFunc &fn : asset.info.funcs) {
            if (fn.type == dccore::kFnObjAnime) add_anim(fn.target, int(fn.pos[0]), int(fn.pos[1]), int(fn.pos[2]), fn.rot, fn.scale, fn.params);
            else if (fn.type == dccore::kFnTimer) {
                auto it = inst_frames_[ii].find(dccore::frame_key(fn.target));
                if (it != inst_frames_[ii].end())
                    for (const FrameRef &fr : it->second) timers_.push_back({fr.node, fn.t_start, fn.t_end, int(ii)});
            }
        }
        for (const dccore::MapAnim *ma : owner_anims) add_anim(ma->target, ma->kind, ma->mode, 0, ma->init, ma->delta, ma->end);
        };
    for (size_t ii = 0; ii < town_->instances.size(); ++ii) build_instance_(ii);
    // the ground of the edit areas
    for (const dccore::Town::Area &area : town_->areas) {
        // RemakeGrid: cells that hold water (code 0x81) have no ground
        dccore::StaticModel filtered = area.grid;
        const float unit = area.def.unit;
        for (dccore::Surface &sf : filtered.surfaces) {
            std::vector<uint32_t> keep;
            for (size_t t = 0; t + 2 < sf.indices.size(); t += 3) {
                float cx = 0, cz = 0;
                for (int k = 0; k < 3; ++k) { cx += sf.positions[3 * sf.indices[t + size_t(k)]]; cz += sf.positions[3 * sf.indices[t + size_t(k)] + 2]; }
                const int gx = int(std::floor(cx / 3.0f / unit)), gz = int(std::floor(cz / 3.0f / unit));
                const bool water = gx >= 0 && gz >= 0 && gx < area.def.width && gz < area.def.height && area.code[size_t(gz * area.def.width + gx)] == 0x81;
                if (!water) keep.insert(keep.end(), {sf.indices[t], sf.indices[t + 1], sf.indices[t + 2]});
            }
            sf.indices = std::move(keep);
        }
        Ref<ArrayMesh> gm = mesh_of(filtered, material_for);
        if (gm.is_null() || gm->get_surface_count() == 0) continue;
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_mesh(gm);
        mi->set_position(Vector3(area.def.origin[0], area.def.origin[1], area.def.origin[2]));
        add_child(mi);
    }
    // water: the ripple / river surface models of lakes and rivers, a little transparent, the texture slides slowly
    auto water_materials = std::make_shared<std::map<std::string, Ref<StandardMaterial3D>>>();
    auto water_material = [this, water_materials, material_for](const std::string &tex) -> Ref<StandardMaterial3D> {
        auto it = water_materials->find(tex);
        if (it != water_materials->end()) return it->second;
        Ref<StandardMaterial3D> m = material_for(tex)->duplicate();
        m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        m->set_albedo(Color(1, 1, 1, 0.72f));
        m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        m->set_roughness(0.15f);
        m->set_specular(0.6f);
        (*water_materials)[tex] = m;
        water_mats_.push_back(m);
        return m;
    };
    std::vector<Ref<ArrayMesh>> water_meshes(town_->assets.size());
    for (size_t i = 0; i < town_->assets.size(); ++i) water_meshes[i] = mesh_of(town_->assets[i].water, water_material);
    water_meshes_ = water_meshes;
    water_material_ = water_material;
    for (size_t ii = 0; ii < town_->instances.size(); ++ii) add_water(int(ii));
    build_sheets();
    set_process(true);
    // the sky: unlit, drawn behind everything, moves with the camera
    sky_root_ = memnew(Node3D);
    add_child(sky_root_);
    auto sky_material = [&](const std::string &tex) -> Ref<StandardMaterial3D> {
        Ref<StandardMaterial3D> m = material_for(tex);
        Ref<StandardMaterial3D> s = m->duplicate();
        s->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        s->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        s->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, true);
        s->set_depth_draw_mode(BaseMaterial3D::DEPTH_DRAW_DISABLED);
        s->set_flag(BaseMaterial3D::FLAG_DISABLE_DEPTH_TEST, false);
        s->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        if (!town_->textures.count(tex)) s->set_albedo(Color(0, 0, 0, 0));   // a texture the game does not have either: draw nothing
        return s;
    };
    int order = 0;
    for (const auto &[n, model] : town_->skies) {
        Ref<ArrayMesh> mesh = mesh_of(model, sky_material);
        if (mesh.is_null() || mesh->get_surface_count() == 0) continue;
        Node3D *layer = memnew(Node3D);
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_mesh(mesh);
        mi->set_sorting_offset(-1000.0f + float(order++));
        layer->add_child(mi);
        sky_root_->add_child(layer);
        sky_layers_[n] = layer;
    }
    for (const auto &[n, model] : town_->suns) {
        Ref<ArrayMesh> mesh = mesh_of(model, sky_material);
        if (mesh.is_null() || mesh->get_surface_count() == 0) continue;
        Node3D *layer = memnew(Node3D);
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_mesh(mesh);
        mi->set_sorting_offset(-900.0f + float(order++));
        layer->add_child(mi);
        sky_root_->add_child(layer);
        sky_layers_[10 + n] = layer;
    }
    update_sky(0.0, Vector3());
}

Dictionary DcTown::get_info() const {
    Dictionary d;
    if (!town_) return d;
    d["name"] = String(town_->name.c_str());
    d["instances"] = int64_t(town_->instances.size());
    d["assets"] = int64_t(town_->assets.size());
    d["events"] = int64_t(town_->events.size());
    d["people"] = int64_t(town_->people.size());
    d["failed"] = int64_t(town_->failed.size());
    d["bgm"] = town_->info.bgm;
    d["sound_set"] = town_->info.sound_set;
    d["time_stop"] = town_->info.time_stop;
    return d;
}

Dictionary DcTown::get_light(double t) const {
    Dictionary d;
    if (!town_) return d;
    t = std::fmod(std::fmod(t, 12.0) + 12.0, 12.0);
    const int i = int(std::floor(t)), j = (i + 1) % 12;
    const float w = float(t - double(i));
    const dccore::MapLight &a = town_->info.lights[size_t(i)], &b = town_->info.lights[size_t(j)];
    auto mix = [&](const float *x, const float *y, int n, std::vector<float> &out) {
        out.resize(size_t(n));
        for (int k = 0; k < n; ++k) out[size_t(k)] = x[k] + (y[k] - x[k]) * w;
    };
    std::vector<float> v;
    mix(a.ambient, b.ambient, 3, v);
    d["ambient"] = Color(v[0] / 128.0f, v[1] / 128.0f, v[2] / 128.0f);
    // directional lights: the first light of each set (the others are unused on the disc)
    Array lights;
    for (int k = 0; k < 4; ++k) {
        if (!a.has_dir[k] && !b.has_dir[k]) continue;
        const dccore::MapLight &sa = a.has_dir[k] ? a : b, &sb = b.has_dir[k] ? b : a;
        Vector3 da(sa.dir[k][0], sa.dir[k][1], sa.dir[k][2]), db(sb.dir[k][0], sb.dir[k][1], sb.dir[k][2]);
        Vector3 dir = (da + (db - da) * w).normalized();
        mix(sa.color[k], sb.color[k], 3, v);
        Dictionary ld;
        ld["direction"] = dir;   // towards the light
        ld["color"] = Color(v[0] / 128.0f, v[1] / 128.0f, v[2] / 128.0f);
        lights.push_back(ld);
    }
    d["lights"] = lights;
    if (a.has_fog || b.has_fog) {
        const dccore::MapLight &fa = a.has_fog ? a : b, &fb = b.has_fog ? b : a;
        mix(fa.fog, fb.fog, 7, v);
        d["fog_near"] = v[0];
        d["fog_far"] = v[1];
        d["fog_color"] = Color(v[2] / 255.0f, v[3] / 255.0f, v[4] / 255.0f);
        d["fog_alpha_near"] = v[5];
        d["fog_alpha_far"] = v[6];
    }
    if (a.has_bg || b.has_bg) {
        const dccore::MapLight &ba = a.has_bg ? a : b, &bb = b.has_bg ? b : a;
        mix(ba.bg_top, bb.bg_top, 3, v);
        d["bg_top"] = Color(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f);
        mix(ba.bg_bottom, bb.bg_bottom, 3, v);
        d["bg_bottom"] = Color(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f);
    }
    return d;
}

void DcTown::step_anims() {
    for (Anim &a : anims_) {
        if (!a.started) { a.started = true; for (int k = 0; k < 3; ++k) a.cur[k] = a.init[k]; }
        switch (a.mode) {
            case 0: for (int k = 0; k < 3; ++k) a.cur[k] += a.delta[k]; break;
            case 1:
                for (int k = 0; k < 3; ++k) {
                    a.cur[k] += a.delta[k];
                    if ((a.delta[k] > 0 && a.cur[k] >= a.end[k]) || (a.delta[k] < 0 && a.cur[k] <= a.end[k])) a.cur[k] = a.init[k];
                }
                break;
            case 2:
                for (int k = 0; k < 3; ++k) {
                    a.cur[k] += a.delta[k] * float(a.dir);
                    const float lo = std::min(a.init[k], a.end[k]), hi = std::max(a.init[k], a.end[k]);
                    if (a.cur[k] > hi) { a.cur[k] = hi; a.dir = -a.dir; }
                    if (a.cur[k] < lo) { a.cur[k] = lo; a.dir = -a.dir; }
                }
                break;
            case 3:
                for (int k = 0; k < 3; ++k) {
                    if (a.delta[k] == 0) continue;
                    a.cur[k] += a.delta[k];
                    if ((a.delta[k] > 0 && a.cur[k] > a.end[k]) || (a.delta[k] < 0 && a.cur[k] < a.end[k])) a.cur[k] = a.end[k];
                }
                break;
            case 4: for (int k = 0; k < 3; ++k) a.cur[k] = a.init[k] + (a.end[k] - a.init[k]) * frand(); break;
            case 6: { const float r = frand(); for (int k = 0; k < 3; ++k) a.cur[k] = a.init[k] + (a.end[k] - a.init[k]) * r; break; }
            default: {   // 5 and 7: random walks clamped to the range
                const float r = a.mode == 7 ? frand() : 0.0f;
                for (int k = 0; k < 3; ++k) {
                    const float d = (a.mode == 7 ? r : frand()) * 2.0f - 1.0f;
                    a.cur[k] += d * std::fabs(a.delta[k]);
                    const float lo = std::min(a.init[k], a.end[k]), hi = std::max(a.init[k], a.end[k]);
                    a.cur[k] = std::clamp(a.cur[k], lo, hi);
                }
                break;
            }
        }
        if (!a.node) continue;
        constexpr float kDeg = 3.14159265f / 180.0f;
        if (a.kind == 0) a.node->set_basis(Basis::from_euler(Vector3(a.cur[0] * kDeg, a.cur[1] * kDeg, a.cur[2] * kDeg)));
        else if (a.kind == 1) a.node->set_position(a.pivot + Vector3(a.cur[0], a.cur[1], a.cur[2]));
        else if (a.kind == 2) a.node->set_scale(Vector3(a.cur[0], a.cur[1], a.cur[2]));
    }
}

Ref<ArrayMesh> DcTown::asset_mesh(int asset) {
    if (asset < 0 || size_t(asset) >= town_->assets.size()) return Ref<ArrayMesh>();
    if (asset_meshes_.size() < town_->assets.size()) asset_meshes_.resize(town_->assets.size());
    if (asset_meshes_[size_t(asset)].is_null()) asset_meshes_[size_t(asset)] = mesh_of(town_->assets[size_t(asset)].lod[0], material_for_);
    return asset_meshes_[size_t(asset)];
}

Ref<ArrayMesh> DcTown::asset_water_mesh(int asset) {
    if (asset < 0 || size_t(asset) >= town_->assets.size()) return Ref<ArrayMesh>();
    if (water_meshes_.size() < town_->assets.size()) water_meshes_.resize(town_->assets.size());
    if (water_meshes_[size_t(asset)].is_null()) water_meshes_[size_t(asset)] = mesh_of(town_->assets[size_t(asset)].water, water_material_);
    return water_meshes_[size_t(asset)];
}

void DcTown::refresh_instance(int index) {
    if (index < 0) return;
    if (size_t(index) >= instance_nodes_.size()) {
        inst_frames_.resize(town_->instances.size());
        instance_nodes_.resize(town_->instances.size(), nullptr);
        inst_extra_.resize(town_->instances.size());
    }
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Anim &a) { return a.instance == index; }), anims_.end());
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](const Timer &t) { return t.instance == index; }), timers_.end());
    inst_frames_[size_t(index)].clear();
    if (Node3D *old = instance_nodes_[size_t(index)]) { remove_child(old); old->queue_free(); }
    instance_nodes_[size_t(index)] = nullptr;
    if (town_->instances[size_t(index)].removed) return;
    if (build_instance_) build_instance_(size_t(index));
    add_water(index);
}

Node3D *DcTown::make_ghost(int asset) {
    Ref<ArrayMesh> m = asset_mesh(asset);
    if (m.is_null()) return nullptr;
    MeshInstance3D *mi = memnew(MeshInstance3D);
    mi->set_mesh(m);
    return mi;
}

void DcTown::add_water(int ii) {
    const dccore::TownInstance &in = town_->instances[size_t(ii)];
    Ref<ArrayMesh> wm = asset_water_mesh(in.asset);
    if (wm.is_null() || wm->get_surface_count() == 0) return;
    Node3D *root = instance_nodes_[size_t(ii)];
    if (!root) {
        root = memnew(Node3D);
        root->set_basis(Basis(Vector3(0, 1, 0), in.yaw));
        root->set_position(Vector3(in.pos[0], in.pos[1], in.pos[2]));
        add_child(root);
        instance_nodes_[size_t(ii)] = root;
    }
    MeshInstance3D *mi = memnew(MeshInstance3D);
    mi->set_mesh(wm);
    root->add_child(mi);
}

void DcTown::set_instance_offset(int index, float dy) {
    if (index < 0 || size_t(index) >= instance_nodes_.size() || !instance_nodes_[size_t(index)]) return;
    const dccore::TownInstance &in = town_->instances[size_t(index)];
    instance_nodes_[size_t(index)]->set_position(Vector3(in.pos[0], in.pos[1] + dy, in.pos[2]));
}

int DcTown::add_instance(int index) {
    const size_t ii = index >= 0 ? size_t(index) : town_->instances.size() - 1;
    inst_frames_.resize(town_->instances.size());
    instance_nodes_.resize(town_->instances.size(), nullptr);
    inst_extra_.resize(town_->instances.size());
    if (build_instance_) build_instance_(ii);
    add_water(int(ii));
    return int(ii);
}

void DcTown::remove_instance(int index) {
    if (index < 0 || size_t(index) >= instance_nodes_.size()) return;
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Anim &a) { return a.instance == index; }), anims_.end());
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](const Timer &t) { return t.instance == index; }), timers_.end());
    inst_frames_[size_t(index)].clear();
    if (Node3D *n = instance_nodes_[size_t(index)]) { remove_child(n); n->queue_free(); }
    instance_nodes_[size_t(index)] = nullptr;
    town_->instances[size_t(index)].visible = false;
}

void DcTown::ensure_frame(int instance, const std::string &key) {
    if (instance < 0 || size_t(instance) >= inst_frames_.size() || !build_instance_) return;
    if (inst_frames_[size_t(instance)].count(key)) return;
    inst_extra_[size_t(instance)].insert(key);
    // rebuild the instance with that frame split out
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(), [&](const Anim &a) { return a.instance == instance; }), anims_.end());
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(), [&](const Timer &t) { return t.instance == instance; }), timers_.end());
    inst_frames_[size_t(instance)].clear();
    if (Node3D *old = instance_nodes_[size_t(instance)]) { remove_child(old); old->queue_free(); }
    instance_nodes_[size_t(instance)] = nullptr;
    build_instance_(size_t(instance));
    add_water(instance);
}

void DcTown::set_frame_visible(int instance, const String &frame, bool visible) {
    if (instance < 0 || size_t(instance) >= inst_frames_.size()) return;
    ensure_frame(instance, dccore::frame_key(frame.utf8().get_data()));
    auto it = inst_frames_[size_t(instance)].find(dccore::frame_key(frame.utf8().get_data()));
    if (it == inst_frames_[size_t(instance)].end()) return;
    for (FrameRef &f : it->second) f.node->set_visible(visible);
}

void DcTown::set_frame_transform(int instance, const String &frame, const Vector3 &pos, const Vector3 &rot, bool set_pos, bool set_rot) {
    if (instance < 0 || size_t(instance) >= inst_frames_.size()) return;
    ensure_frame(instance, dccore::frame_key(frame.utf8().get_data()));
    auto it = inst_frames_[size_t(instance)].find(dccore::frame_key(frame.utf8().get_data()));
    if (it == inst_frames_[size_t(instance)].end()) return;
    for (FrameRef &f : it->second) {
        if (set_pos) f.node->set_position(pos);   // the script positions are absolute (the frame's own position replaced)
        if (set_rot) f.node->set_basis(Basis::from_euler(rot));
    }
}

void DcTown::_process(double delta) {
    anim_accum_ += delta * 60.0;
    int n = int(anim_accum_);
    anim_accum_ -= n;
    if (n > 4) n = 4;
    for (int i = 0; i < n; ++i) {
        step_anims();
        for (Sheet &s : sheets_) step_sheet(s);
    }
    if (!sheets_.empty()) {
        Vector3 cp, cd(0, 0, 1);
        if (Viewport *vp = get_viewport())
            if (Camera3D *cam = vp->get_camera_3d()) { cp = cam->get_global_position(); cd = -cam->get_global_transform().basis.get_column(2); }
        for (Sheet &s : sheets_) update_sheet(s, cp, cd);
    }
    // timer frames: shown inside their clock window (ConvertTime of hours; wraps over midnight)
    const double hours = std::fmod(clock_ * 2.0 + 10.0, 24.0);
    for (const Timer &t : timers_) {
        bool on;
        if (t.t0 <= t.t1) on = hours >= t.t0 && hours < t.t1;
        else on = hours >= t.t0 || hours < t.t1;
        if (t.t1 >= 24.0f && t.t0 <= 0.0f) on = true;
        if (t.node) t.node->set_visible(on);
    }
    water_time_ += delta;
    for (Ref<StandardMaterial3D> &m : water_mats_) m->set_uv1_offset(Vector3(float(water_time_ * 0.02), float(water_time_ * 0.03), 0));
}

void DcTown::update_sky(double t, const Vector3 &camera_pos) {
    if (!sky_root_) return;
    t = std::fmod(std::fmod(t, 12.0) + 12.0, 12.0);
    const int phase = std::min(3, int(t / 3.0)) + 1;   // sky 1 day, 2 dusk, 3 night, 4 morning
    for (auto &[n, node] : sky_layers_) {
        const bool plate = n == 5;
        const bool sun = n > 10;
        node->set_visible(plate || (!sun && n == phase) || (sun && n - 10 == phase));
    }
    const dccore::MapInfo &mi = town_->info;
    sky_root_->set_position(Vector3(mi.sky_follow[0] ? camera_pos.x : 0, mi.sky_follow[1] ? camera_pos.y : 0, mi.sky_follow[2] ? camera_pos.z : 0));
}

Array DcTown::get_events() const {
    Array out;
    if (!town_) return out;
    for (size_t i = 0; i < town_->events.size(); ++i) {
        const dccore::TownEvent &e = town_->events[i];
        Dictionary d;
        d["index"] = int(i);
        d["kind"] = e.kind;
        d["map"] = String(e.map.c_str());
        d["jump_id"] = e.jump_id;
        d["pos"] = Vector3(e.pos[0], e.pos[1], e.pos[2]);
        d["yaw"] = e.yaw;
        d["extent"] = e.extent;
        d["event_no"] = e.event_no;
        d["item"] = e.item;
        d["count"] = e.count;
        d["flag"] = e.flag;
        d["has_out"] = e.has_out;
        d["out_pos"] = Vector3(e.out_pos[0], e.out_pos[1], e.out_pos[2]);
        out.push_back(d);
    }
    return out;
}

Array DcTown::get_people() const {
    Array out;
    if (!town_) return out;
    for (const dccore::TownPerson &p : town_->people) {
        Dictionary d;
        d["villager"] = p.villager;
        d["pos"] = Vector3(p.pos[0], p.pos[1], p.pos[2]);
        d["yaw"] = p.yaw;
        if (size_t(p.villager) < town_->info.people.size()) d["model"] = String(town_->info.people[size_t(p.villager)].name.c_str());
        out.push_back(d);
    }
    return out;
}


// ---- the water sheets (CWater) ------------------------------------------------------------------------------------------------------------------------------

void DcTown::build_sheets() {
    sheets_.clear();
    sheets_.reserve(town_->info.water_surfaces.size());
    for (const dccore::WaterSurfaceDef &w : town_->info.water_surfaces) {
        if (w.nx < 3 || w.nz < 3) continue;
        Sheet s;
        s.def = &w;
        s.h[0].assign(size_t(w.nx * w.nz), 0.0f);
        s.h[1].assign(size_t(w.nx * w.nz), 0.0f);
        s.mesh.instantiate();
        std::string tex = w.texture;
        if (tex.empty() || !town_->textures.count(tex)) tex = town_->water_textures.empty() ? std::string() : town_->water_textures.front();
        Ref<StandardMaterial3D> m = material_for_(tex)->duplicate();
        m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        m->set_albedo(Color(w.rgb[0] / 128.0f, w.rgb[1] / 128.0f, w.rgb[2] / 128.0f, 0.5f));   // SetColor(r, g, b, 0x80)
        m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
        m->set_roughness(0.15f);
        m->set_specular(0.6f);
        water_mats_.push_back(m);
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_mesh(s.mesh);
        mi->set_material_override(m);
        mi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        mi->set_extra_cull_margin(400.0f);
        add_child(mi);
        s.node = mi;
        sheets_.push_back(std::move(s));
    }
}

// Hamon: one step of the wave equation; Shake: the sources of WATER_SHAKE (a negative cell = a random one every tick)
void DcTown::step_sheet(Sheet &s) {
    const dccore::WaterSurfaceDef &w = *s.def;
    for (size_t k = 0; k < w.shakes.size() && k < 4; ++k) {
        const auto &sh = w.shakes[k];
        if (sh[2] == 0.0f && sh[3] == 0.0f) break;
        int x = int(sh[0]), z = int(sh[1]);
        if (x < 0) x = int(frand() * float(w.nx));
        if (z < 0) z = int(frand() * float(w.nz));
        x = std::clamp(x % w.nx, 1, w.nx - 2);
        z = std::clamp(z % w.nz, 1, w.nz - 2);
        s.h[size_t(s.cur)][size_t(x * w.nz + z)] += sh[3] + sh[2] * frand();
    }
    const int nxt = 1 - s.cur;
    std::vector<float> &cur = s.h[size_t(s.cur)], &nw = s.h[size_t(nxt)];
    const float c2 = w.params[0] * w.params[0], damp = w.params[1];
    const int nz = w.nz;
    for (int i = 1; i < w.nx - 1; ++i)
        for (int j = 1; j < nz - 1; ++j) {
            const size_t k = size_t(j + i * nz);
            const float prev = nw[k];
            nw[k] = ((cur[k - size_t(nz)] + cur[k - 1] + cur[k + 1] + cur[k + size_t(nz)]) * c2 + ((1.0f - c2 * 2.0f) * 2.0f * cur[k] - prev)) - damp * (cur[k] - prev);
        }
    s.cur = nxt;
}

void DcTown::update_sheet(Sheet &s, const Vector3 &camera_pos, const Vector3 &camera_dir) {
    const dccore::WaterSurfaceDef &w = *s.def;
    // the sheet follows the camera on the axes the config asks for (extra flags), 50 units ahead
    Vector3 dir(camera_dir.x, 0, camera_dir.z);
    if (dir.length() > 0.0001f) dir = dir.normalized();
    Vector3 pos(w.origin[0], w.origin[1], w.origin[2]);
    if (w.extra[0]) pos.x = camera_pos.x + dir.x * 50.0f;
    if (w.extra[1]) pos.y = camera_pos.y;
    if (w.extra[2]) pos.z = camera_pos.z + dir.z * 50.0f;
    s.node->set_position(pos);
    const std::vector<float> &hgt = s.h[size_t(s.cur)];
    PackedVector3Array v, n;
    PackedVector2Array uv;
    PackedInt32Array idx;
    v.resize(w.nx * w.nz);
    n.resize(w.nx * w.nz);
    uv.resize(w.nx * w.nz);
    const float tile = w.params[3] > 0.0f ? w.params[3] : 1.0f;
    for (int i = 0; i < w.nx; ++i)
        for (int j = 0; j < w.nz; ++j) {
            const float fx = float(i) / float(w.nx - 1), fz = float(j) / float(w.nz - 1);
            const size_t k = size_t(j + i * w.nz);
            v.set(int(k), Vector3(w.p0[0] + (w.p1[0] - w.p0[0]) * fx, w.p0[1] + hgt[k], w.p0[2] + (w.p1[2] - w.p0[2]) * fz));
            uv.set(int(k), Vector2(fx * tile, fz * tile));
            const float dx = (i > 0 && i < w.nx - 1) ? hgt[k + size_t(w.nz)] - hgt[k - size_t(w.nz)] : 0.0f;
            const float dz = (j > 0 && j < w.nz - 1) ? hgt[k + 1] - hgt[k - 1] : 0.0f;
            n.set(int(k), Vector3(-dx, 2.0f, -dz).normalized());
        }
    for (int i = 0; i + 1 < w.nx; ++i)
        for (int j = 0; j + 1 < w.nz; ++j) {
            const int a = j + i * w.nz, b = a + 1, c = a + w.nz, d = c + 1;
            for (int t : {a, c, b, b, c, d}) idx.push_back(t);
        }
    Array arr;
    arr.resize(Mesh::ARRAY_MAX);
    arr[Mesh::ARRAY_VERTEX] = v;
    arr[Mesh::ARRAY_NORMAL] = n;
    arr[Mesh::ARRAY_TEX_UV] = uv;
    arr[Mesh::ARRAY_INDEX] = idx;
    s.mesh->clear_surfaces();
    s.mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arr);
}
