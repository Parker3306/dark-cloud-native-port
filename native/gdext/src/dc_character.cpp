#include "dc_character.h"

#include <cmath>
#include <dccore/recolor.h>
#include <map>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/skin.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

using namespace godot;

namespace {

// Row-vector 4x4 (rows = axes, last row = translation) -> Godot's column-vector Transform3D.
Transform3D to_transform(const dccore::Mat4 &m) {
    const Vector3 x(static_cast<real_t>(m[0]), static_cast<real_t>(m[1]), static_cast<real_t>(m[2])), y(static_cast<real_t>(m[4]), static_cast<real_t>(m[5]), static_cast<real_t>(m[6])), z(static_cast<real_t>(m[8]), static_cast<real_t>(m[9]), static_cast<real_t>(m[10]));
    const Vector3 o(static_cast<real_t>(m[12]), static_cast<real_t>(m[13]), static_cast<real_t>(m[14]));
    return Transform3D(Basis(x, y, z), o);
}

Ref<ImageTexture> to_texture(const dccore::Image &im) {
    PackedByteArray data;
    data.resize(int64_t(im.rgba.size()));
    std::memcpy(data.ptrw(), im.rgba.data(), im.rgba.size());
    Ref<Image> img = Image::create_from_data(int(im.width), int(im.height), false, Image::FORMAT_RGBA8, data);
    img->generate_mipmaps();
    return ImageTexture::create_from_image(img);
}

// Root-relative world matrices: the skin object's parent is treated as identity (see dccore/motion.h).
std::vector<dccore::Mat4> relative_to_root(const std::vector<dccore::Mat4> &world, int root) {
    std::vector<dccore::Mat4> out = world;
    if (root < 0 || size_t(root) >= world.size()) return out;
    dccore::Mat4 inv = dccore::mat_inverse(world[root]);
    for (size_t i = 0; i < world.size(); ++i) out[i] = dccore::mat_mul(world[i], inv);
    return out;
}

} // namespace

void DcCharacter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_motion_count"), &DcCharacter::get_motion_count);
    ClassDB::bind_method(D_METHOD("get_motion_info", "index"), &DcCharacter::get_motion_info);
    ClassDB::bind_method(D_METHOD("play", "motion", "loop"), &DcCharacter::play, DEFVAL(true));
    ClassDB::bind_method(D_METHOD("set_time", "frame"), &DcCharacter::set_time);
    ClassDB::bind_method(D_METHOD("get_time"), &DcCharacter::get_time);
    ClassDB::bind_method(D_METHOD("set_playing", "playing"), &DcCharacter::set_playing);
    ClassDB::bind_method(D_METHOD("is_playing"), &DcCharacter::is_playing);
    ClassDB::bind_method(D_METHOD("set_speed_scale", "scale"), &DcCharacter::set_speed_scale);
    ClassDB::bind_method(D_METHOD("get_speed_scale"), &DcCharacter::get_speed_scale);
    ClassDB::bind_method(D_METHOD("get_motion"), &DcCharacter::get_motion);
    ClassDB::bind_method(D_METHOD("get_joint_names"), &DcCharacter::get_joint_names);
    ClassDB::bind_method(D_METHOD("sample_camera", "frame"), &DcCharacter::sample_camera);
    ClassDB::bind_method(D_METHOD("find_joint", "name"), &DcCharacter::find_joint);
    ClassDB::bind_method(D_METHOD("set_player_color", "index"), &DcCharacter::set_player_color);
    ClassDB::bind_method(D_METHOD("get_player_color"), &DcCharacter::get_player_color);
    ClassDB::bind_method(D_METHOD("set_joint_override", "joint", "rot", "pos", "set_rot", "set_pos"), &DcCharacter::set_joint_override);
}

int DcCharacter::get_motion_count() const { return ch_ ? int(ch_->motions.size()) : 0; }

Dictionary DcCharacter::get_motion_info(int index) const {
    Dictionary d;
    const dccore::MotionEntry *me = ch_ ? ch_->motion_entry(index) : nullptr;
    if (!me) return d;
    const auto &m = *me;
    d["start"] = m.start;
    d["end"] = m.end;
    d["speed"] = m.speed;
    return d;
}

PackedStringArray DcCharacter::get_joint_names() const {
    PackedStringArray a;
    if (ch_) for (const auto &j : ch_->joints) a.push_back(String(j.name.c_str()));
    return a;
}

void DcCharacter::play(int motion, bool loop) {
    const dccore::MotionEntry *me = ch_ ? ch_->motion_entry(motion) : nullptr;
    if (!me) return;
    motion_ = motion;
    loop_ = loop;
    auto ex = ch_->extra_motions.find(motion);
    cur_set_ = ex != ch_->extra_motions.end() ? ex->second.set : -1;
    time_ = me->start;
    playing_ = true;
    apply_pose();
}

void DcCharacter::set_time(double frame) {
    time_ = frame;
    apply_pose();
}

void DcCharacter::_process(double delta) {
    const dccore::MotionEntry *me = ch_ ? ch_->motion_entry(motion_) : nullptr;
    if (!me || !playing_) return;
    const auto &m = *me;
    time_ += double(m.speed) * speed_scale_ * 60.0 * delta;  // the game advances `speed` frames per tick at 60 ticks per second
    if (std::floor(time_) >= m.end) {
        if (loop_) time_ -= double(m.end - m.start);  // same wrap as the game's SetMotionEX
        else { time_ = m.end; playing_ = false; }
    }
    apply_pose();
}

Transform3D DcCharacter::attach_transform(int joint, const dccore::Mat4 &local) const {
    if (joint < 0 || size_t(joint) >= pose_.world.size()) return Transform3D();
    return to_transform(pose_.world[size_t(joint)]) * to_transform(local);
}

void DcCharacter::set_joint_override(int joint, const Vector3 &rot, const Vector3 &pos, bool set_rot, bool set_pos) {
    if (joint < 0) return;
    JointOverride &o = joint_override_[joint];
    if (set_rot) { o.rot = rot; o.set_rot = true; }
    if (set_pos) { o.pos = pos; o.set_pos = true; }
    apply_pose();
}

void DcCharacter::apply_pose() {
    if (!ch_ || !skeleton_) return;
    pose_ = dccore::evaluate_pose(ch_->skeleton, cur_set_ >= 0 && size_t(cur_set_) < ch_->extra_sets.size() ? ch_->extra_sets[size_t(cur_set_)] : ch_->tracks, time_);
    if (!joint_override_.empty()) {
        for (const auto &kv : joint_override_) {
            if (size_t(kv.first) >= pose_.local.size()) continue;
            dccore::Mat4 &m = pose_.local[size_t(kv.first)];
            if (kv.second.set_rot) {   // row-vector rotation: Rx * Ry * Rz of the script's angles (the frame keeps its place)
                const double cx = std::cos(double(kv.second.rot.x)), sx = std::sin(double(kv.second.rot.x));
                const double cy = std::cos(double(kv.second.rot.y)), sy = std::sin(double(kv.second.rot.y));
                const double cz = std::cos(double(kv.second.rot.z)), sz = std::sin(double(kv.second.rot.z));
                const double rx[9] = {1, 0, 0, 0, cx, sx, 0, -sx, cx};
                const double ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy};
                const double rz[9] = {cz, sz, 0, -sz, cz, 0, 0, 0, 1};
                double t[9], r[9];
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { t[i * 3 + j] = 0; for (int k = 0; k < 3; ++k) t[i * 3 + j] += rx[i * 3 + k] * ry[k * 3 + j]; }
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { r[i * 3 + j] = 0; for (int k = 0; k < 3; ++k) r[i * 3 + j] += t[i * 3 + k] * rz[k * 3 + j]; }
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) m[size_t(i * 4 + j)] = r[i * 3 + j];
            }
            if (kv.second.set_pos) { m[12] = double(kv.second.pos.x); m[13] = double(kv.second.pos.y); m[14] = double(kv.second.pos.z); }
        }
        const dccore::Skeleton &sk = ch_->skeleton;
        for (size_t i = 0; i < sk.size(); ++i) {   // the worlds again (parents come first)
            const int par = sk.parent[i];
            if (par < 0 || size_t(par) >= i) { pose_.world[i] = pose_.local[i]; continue; }
            const dccore::Mat4 &a = pose_.local[i], &b = pose_.world[size_t(par)];
            dccore::Mat4 w{};
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { double s = 0; for (int k = 0; k < 4; ++k) s += a[size_t(r * 4 + k)] * b[size_t(k * 4 + c)]; w[size_t(r * 4 + c)] = s; }
            pose_.world[i] = w;
        }
    }
    const dccore::Pose &pose = pose_;
    // Bones and rigid parts use the full animated world matrices. Skinned vertices are skinned relative to the skin's parent (binds below) and then drawn
    // through that parent's world matrix by the game; with full-world bone poses the parent's matrix cancels out, so the body stays attached to the rigid parts.
    for (size_t i = 0; i < pose.world.size(); ++i) skeleton_->set_bone_global_pose(int(i), to_transform(pose.world[i]));
    for (const Rigid &r : rigid_) r.node->set_transform(to_transform(pose.world[size_t(r.object)]));
    if (held_ && held_joint_ >= 0) {
        dccore::Mat4 identity{};
        identity[0] = identity[5] = identity[10] = identity[15] = 1.0;
        held_->set_transform(attach_transform(held_joint_, identity));
    }
}

void DcCharacter::setup(std::shared_ptr<dccore::Character> ch, Dictionary &info) {
    ch_ = std::move(ch);
    const dccore::Skeleton &sk = ch_->skeleton;

    skeleton_ = memnew(Skeleton3D);
    skeleton_->set_name("Skeleton");
    add_child(skeleton_);
    for (size_t i = 0; i < sk.size(); ++i) {
        String name = String(sk.names[i].c_str());
        if (skeleton_->find_bone(name) >= 0 || name.is_empty()) name = name + "_" + String::num_int64(int64_t(i));
        skeleton_->add_bone(name);
    }
    for (size_t i = 0; i < sk.size(); ++i)
        if (sk.parent[i] >= 0 && size_t(sk.parent[i]) < i) skeleton_->set_bone_parent(int(i), sk.parent[i]);

    // Skin binds (per skin root): inverse of the bind-pose world matrices relative to the skin's parent.
    std::map<int, Ref<Skin>> skins;
    auto skin_for = [&](int root) -> Ref<Skin> {
        auto it = skins.find(root);
        if (it != skins.end()) return it->second;
        Ref<Skin> skin;
        skin.instantiate();
        std::vector<dccore::Mat4> bind = relative_to_root(sk.bind_world, root);
        for (size_t i = 0; i < sk.size(); ++i) skin->add_bind(int(i), to_transform(dccore::mat_inverse(bind[i])));
        skins[root] = skin;
        return skin;
    };

    std::map<std::string, Ref<StandardMaterial3D>> materials;
    auto material_for = [&](const std::string &tex) -> Ref<StandardMaterial3D> {
        auto it = materials.find(tex);
        if (it != materials.end()) return it->second;
        Ref<StandardMaterial3D> m;
        m.instantiate();
        m->set_roughness(0.9f);
        m->set_metallic(0.0f);
        auto t = ch_->textures.find(tex);
        if (t != ch_->textures.end()) {
            m->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, to_texture(t->second));
            m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
            m->set_alpha_scissor_threshold(0.5f);
            m->set_texture_filter(BaseMaterial3D::TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC);
        } else {
            m->set_albedo(Color(0.8f, 0.2f, 0.8f));  // missing texture: magenta
        }
        materials[tex] = m;
        return m;
    };

    int64_t triangles = 0, surfaces = 0, skinned_parts = 0;
    for (const dccore::Part &part : ch_->parts) {
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        for (const dccore::Surface &s : part.surfaces) {
            if (s.indices.empty()) continue;
            PackedVector3Array v, n;
            PackedVector2Array uv;
            PackedInt32Array idx, bones;
            PackedFloat32Array weights;
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
            if (part.skinned) {
                bones.resize(nv * 4);
                weights.resize(nv * 4);
                for (int64_t i = 0; i < nv * 4; ++i) { bones.set(i, s.bones[size_t(i)]); weights.set(i, s.weights[size_t(i)]); }
                arrays[Mesh::ARRAY_BONES] = bones;
                arrays[Mesh::ARRAY_WEIGHTS] = weights;
            }
            mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
            mesh->surface_set_material(mesh->get_surface_count() - 1, material_for(s.texture));
            triangles += int64_t(s.indices.size() / 3);
            ++surfaces;
        }
        if (mesh->get_surface_count() == 0) continue;
        MeshInstance3D *mi = memnew(MeshInstance3D);
        mi->set_name(String(part.name.c_str()));
        mi->set_mesh(mesh);
        if (part.skinned) {
            skeleton_->add_child(mi);
            mi->set_skeleton_path(NodePath(".."));
            mi->set_skin(skin_for(part.skin_root));
            mi->set_custom_aabb(AABB(Vector3(-30, -5, -30), Vector3(60, 40, 60)));  // skinned vertices move outside the rest-pose bounds
            ++skinned_parts;
        } else {
            add_child(mi);
            rigid_.push_back({mi, part.object});
        }
    }
    info["model"] = String(ch_->model_name.c_str());
    info["img"] = String(ch_->img_name.c_str());
    info["parts"] = int64_t(ch_->parts.size());
    info["skinned_parts"] = skinned_parts;
    info["surfaces"] = surfaces;
    info["triangles"] = triangles;
    info["textures"] = int64_t(ch_->textures.size());
    info["joints"] = int64_t(sk.size());
    info["tracks"] = int64_t(ch_->tracks.size());
    info["motions"] = int64_t(ch_->motions.size());

    tex_materials_ = materials;
    time_ = ch_->motions.empty() ? 0.0 : double(ch_->motions[0].start);
    apply_pose();
    set_process(true);
}

namespace godot {

void DcCharacter::set_effect_look(bool on) {
    std::vector<Node *> stack;
    stack.push_back(this);
    while (!stack.empty()) {
        Node *n = stack.back();
        stack.pop_back();
        for (int i = 0; i < n->get_child_count(); ++i) stack.push_back(n->get_child(i));
        MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
        if (!mi || mi->get_mesh().is_null()) continue;
        Ref<Mesh> mesh = mi->get_mesh();
        for (int s = 0; s < mesh->get_surface_count(); ++s) {
            Ref<StandardMaterial3D> m = mesh->surface_get_material(s);
            if (m.is_null()) continue;
            m->set_transparency(on ? BaseMaterial3D::TRANSPARENCY_ALPHA : BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
            m->set_blend_mode(on ? BaseMaterial3D::BLEND_MODE_ADD : BaseMaterial3D::BLEND_MODE_MIX);
            m->set_shading_mode(on ? BaseMaterial3D::SHADING_MODE_UNSHADED : BaseMaterial3D::SHADING_MODE_PER_PIXEL);
            m->set_cull_mode(on ? BaseMaterial3D::CULL_DISABLED : BaseMaterial3D::CULL_BACK);
            m->set_depth_draw_mode(on ? BaseMaterial3D::DEPTH_DRAW_DISABLED : BaseMaterial3D::DEPTH_DRAW_OPAQUE_ONLY);
        }
    }
}

}  // namespace godot

Dictionary DcCharacter::sample_camera(double frame) const {
    Dictionary d;
    if (!ch_) return d;
    float pos[3] = {0, 0, 0}, ref[3] = {0, 0, 0}, fov = 0;
    if (!dccore::sample_camera_tracks(ch_->tracks, frame, pos, ref, &fov)) return d;
    d["pos"] = Vector3(pos[0], pos[1], pos[2]);
    d["ref"] = Vector3(ref[0], ref[1], ref[2]);
    d["fov"] = fov;
    return d;
}

int DcCharacter::find_joint(const String &name) const {
    if (!ch_) return -1;
    std::string key = name.utf8().get_data();
    const size_t cut = key.find("__");
    if (cut != std::string::npos) key.resize(cut);
    for (size_t i = 0; i < ch_->joints.size(); ++i) {
        std::string jn = ch_->joints[i].name;
        const size_t c2 = jn.find("__");
        if (c2 != std::string::npos) jn.resize(c2);
        if (jn == key) return int(i);
    }
    return -1;
}

void DcCharacter::set_player_color(int index) {
    if (!ch_ || index == color_index_) return;
    color_index_ = index;
    for (auto &kv : tex_materials_) {
        auto t = ch_->textures.find(kv.first);
        if (t == ch_->textures.end() || kv.second.is_null()) continue;
        if (index < 0 || dccore::is_face_texture(kv.first, t->second)) {
            kv.second->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, to_texture(t->second));
            continue;
        }
        dccore::Image copy = t->second;
        dccore::recolor_clothes(copy, dccore::player_hue(index));
        if (dccore::is_wash_texture(kv.first)) dccore::wash_cloth(copy, dccore::player_hue(index));
        kv.second->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, to_texture(copy));
    }
}

void DcCharacter::set_held_weapon(DcCharacter *weapon, int joint) {
    if (held_) { remove_child(held_); held_->queue_free(); }
    held_ = weapon;
    held_joint_ = joint;
    if (held_) {
        add_child(held_);
        held_->set_playing(false);
        apply_pose();
    }
}
