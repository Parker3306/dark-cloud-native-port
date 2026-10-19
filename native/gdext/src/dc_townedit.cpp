// Georama edit mode of a town (EditMode / GameMode 4 of the game, notes/georama_edit.md section 4) and the Atla board (section 2.6).
#include "dc_townwalk.h"

#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr uint32_t kBitL2 = 1, kBitR2 = 2;                                   // pad.buttons in edit mode
constexpr uint32_t kBitTriangle = 0x10, kBitSelect = 0x100;                  // menu bits
constexpr uint32_t kDUp = 0x1000, kDRight = 0x2000, kDDown = 0x4000, kDLeft = 0x8000;

const std::vector<std::string> kHeroNames = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

String first_line(const String &s) {
    String l = s.split("\n")[0].strip_edges();
    return l.trim_prefix("\"").trim_suffix("\"");
}

}  // namespace

String DcTownWalk::atla_name(int id) {
    if (!env_.atla_text) return String();
    return String::utf8(env_.atla_text->get(id, kHeroNames).c_str());
}

String DcTownWalk::part_name(int part) {
    if (!geo_ || part < 0 || part >= 24) return String();
    return first_line(atla_name(1000 + 200 * geo_->town_no() + geo_->def(part).name_msg));
}

String DcTownWalk::chip_name(int chip, bool description) {
    if (!geo_ || !env_.geo_tables || chip < 0 || chip >= 60) return String();
    const dccore::GeoElemRow &row = env_.geo_tables->elem[geo_->town_no()][0x28 + chip];
    const int base = 1000 + 200 * geo_->town_no() + row.msg + (chip >= 40 ? 0x28 : 0) + 0x28;
    const String text = atla_name(base + (description ? 0x3c : 0));
    return description ? text : first_line(text);
}

void DcTownWalk::edit_notice(const char *kind, int a) {
    Dictionary d;
    d["kind"] = String(kind);
    d["a"] = a;
    edit_notices_.push_back(d);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// entering and leaving

void DcTownWalk::edit_enter() {
    if (!can_edit() || mode_ != ModeWalk) return;
    mode_ = ModeEdit;
    edit_cam_ = walker_->camera;
    edit_cam_.follow_on = true;
    const bool small = env_.map_index == 4;   // e05 is drawn closer
    edit_cam_.dist = small ? 200.0f : 1000.0f;
    edit_cam_.height = small ? 600.0f : 1000.0f;
    edit_cam_.speed = 10.0f;
    for (int k = 0; k < 3; ++k) cursor_[k] = walker_->player.pos[k];
    edit_cam_.follow[0] = cursor_[0]; edit_cam_.follow[1] = cursor_[1]; edit_cam_.follow[2] = cursor_[2];
    edit_status_ = -1;
    edit_def_ = -1;
    edit_rot_ = 0;
    edit_frames_ = 0;
    edit_hold_ = 0;
    edit_rest_ = 0;
    edit_menu_request_ = false;
    edit_prev_bits_ = prev_menu_;
    edit_prev_buttons_ = 0xffffffffu;
    float none[3] = {cursor_[0], cursor_[1], cursor_[2]};
    edit_limit_cursor(none);
    geo_->request_check();
    edit_notice("enter");
}

// CheckEditToWalk (simplified): the cursor stands in an edit area on a free cell (or on a road)
bool DcTownWalk::edit_check_to_walk(float out_pos[3]) const {
    const int area = dccore::edit_find_area(*town_, cursor_);
    if (area < 0) return false;
    const dccore::Town::Area &a = town_->areas[size_t(area)];
    int cx, cz;
    dccore::edit_cell(a, cursor_, cx, cz);
    if (cx < 0 || cz < 0 || cx >= a.def.width || cz >= a.def.height) return false;
    const size_t ci = size_t(cz * a.def.width + cx);
    const bool road = a.kind[ci] == 1;
    if (!road && (a.code[ci] == 0x80 || a.code[ci] == 0x81)) return false;
    out_pos[0] = cursor_[0];
    out_pos[1] = cursor_[1] + 1.0f;
    out_pos[2] = cursor_[2];
    return true;
}

void DcTownWalk::edit_leave(const float pos[3]) {
    mode_ = ModeWalk;
    if (edit_ghost_) { remove_child(edit_ghost_); edit_ghost_->queue_free(); edit_ghost_ = nullptr; }
    if (edit_outline_) { remove_child(edit_outline_); edit_outline_->queue_free(); edit_outline_ = nullptr; }
    ghost_def_ = -2;
    if (hand_chip_ >= 0) georama_cancel_hand();
    walker_->place_at(pos[0], pos[2], pos[1] + 4.0f);
    walker_->camera.angle = walker_->camera.angle_cur = edit_cam_.angle_cur;
    walker_->camera.follow_on = true;
    dccore::camera_step(walker_->camera, 1);
    for (int i = 0; i < 3; ++i) { walker_->camera.pos[i] = walker_->camera.next_pos[i]; walker_->camera.ref[i] = walker_->camera.next_ref[i]; }
    geo_->request_check();
    geo_->save_layout();
    rebuild_after_edit();
    for (size_t i = 0; i < villager_visible_.size() && i < town_->people.size(); ++i) villager_visible_[i] = villager_present_now(town_->people[i].villager);
    init_roamers();
    edit_notice("leave");
    if (rt_) {   // the events of the finished work: all wishes met (131), the river of Matataki (11)
        if (env_.map_index == 1 && env_.save && !env_.save->game_flag(0x14) && geo_->request(16) && rt_->has_function(11)) pending_event_ = 11;
        else if (!geo_->request_event_flag() && geo_->all_requests_met() && rt_->has_function(131)) pending_event_ = 131;
    }
}

void DcTownWalk::edit_toggle() {
    if (mode_ == ModeWalk) {
        edit_enter();
    } else if (mode_ == ModeEdit && drops_.empty() && !edit_menu_request_) {
        float p[3];
        if (edit_check_to_walk(p)) edit_leave(p);
        else edit_notice("denied");
    }
}

void DcTownWalk::edit_menu_done(int result) {
    edit_menu_request_ = false;
    if (hand_chip_ >= 0) georama_cancel_hand();
    if (result == 1) {
        edit_status_ = 3;
        edit_def_ = -1;
    } else if (!edit_picked_) {
        edit_status_ = -1;
        edit_def_ = -1;
    }
    edit_picked_ = false;
    edit_prev_buttons_ = 0xffffffffu;   // the key that closed the menu must not act on the map
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// the cursor

// LimitEditCursorPos: stay inside the EDITAREA_RECT rectangles (height of the rectangle) and not below the ground of the cell
void DcTownWalk::edit_limit_cursor(const float prev[3]) {
    const auto &rects = town_->info.edit_rects;
    float &x = cursor_[0], &y = cursor_[1], &z = cursor_[2];
    if (rects.empty()) {
        y = 0.0f;
    } else {
        auto inside = [](const std::array<float, 6> &r, float px, float pz) { return px >= r[0] && px <= r[3] && pz >= r[2] && pz <= r[5]; };
        bool in_any = false;
        float best_y = -1e30f;
        for (const auto &r : rects)
            if (inside(r, x, z)) { in_any = true; best_y = std::max(best_y, r[1]); }
        if (in_any) {
            y = best_y;
        } else {
            const bool prev_in = [&]() { for (const auto &r : rects) if (inside(r, prev[0], prev[2])) return true; return false; }();
            const std::array<float, 6> *best = nullptr;
            float best_d = 1e30f;
            for (const auto &r : rects) {
                if (prev_in && !inside(r, prev[0], prev[2])) continue;
                const float dx = std::max({r[0] - x, 0.0f, x - r[3]}), dz = std::max({r[2] - z, 0.0f, z - r[5]});
                if (dx + dz < best_d) { best_d = dx + dz; best = &r; }
            }
            if (best) {
                x = std::clamp(x, (*best)[0], (*best)[3]);
                z = std::clamp(z, (*best)[2], (*best)[5]);
                y = (*best)[1];
            }
        }
    }
    const int area = dccore::edit_find_area(*town_, cursor_);
    edit_area_ = area;
    if (area >= 0) {
        const dccore::Town::Area &a = town_->areas[size_t(area)];
        int cx, cz;
        dccore::edit_cell(a, cursor_, cx, cz);
        if (cx >= 0 && cz >= 0 && cx < a.def.width && cz < a.def.height) {
            const float ground = a.def.origin[1] + float(a.alt[size_t(cz * a.def.width + cx)]) * a.def.unit_alt;
            if (y < ground) y = ground;
        }
    }
}

void DcTownWalk::step_edit(const dccore::PadRaw &pad, uint32_t bits) {
    ++edit_frames_;
    const uint32_t pressed = bits & ~edit_prev_bits_;
    const uint32_t btn_down = pad.buttons & ~edit_prev_buttons_;
    edit_prev_bits_ = bits;
    edit_prev_buttons_ = pad.buttons;
    // the buildings drop into place
    for (auto &d : drops_) {
        --d.second;
        scene_->set_instance_offset(d.first, d.second > 0 ? 50.0f * float(d.second) / 15.0f : 0.0f);
    }
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(), [](const std::pair<int, int> &d) { return d.second <= 0; }), drops_.end());

    bool moved = false;
    if (!edit_menu_request_) {
        float p[3];
        if ((pressed & kBitSelect) && drops_.empty()) {
            if (edit_check_to_walk(p)) { edit_leave(p); return; }
            edit_notice("denied");
        }
        if ((pressed & kBitTriangle) || (btn_down & dccore::kPadNO)) {
            edit_menu_request_ = true;
            edit_notice("menu");
        } else {
            // the camera: right stick turns, L1 / R1 a degree per frame
            const float rx = dccore::pad_axis(pad.rx);
            if (rx != 0.0f) edit_cam_.angle += 0.03f * -rx;
            else if (pad.buttons & dccore::kPadCamRight) edit_cam_.angle += 0.0174533f;
            else if (pad.buttons & dccore::kPadCamLeft) edit_cam_.angle -= 0.0174533f;
            const float prev[3] = {cursor_[0], cursor_[1], cursor_[2]};
            const float ca = std::cos(edit_cam_.angle_cur), sa = std::sin(edit_cam_.angle_cur);
            // the stick moves the cursor freely
            const float lx = dccore::pad_axis(pad.lx), ly = dccore::pad_axis(pad.ly);
            const float scale = env_.map_index == 4 ? 6.0f : 10.0f;
            if (lx != 0.0f || ly != 0.0f) {
                cursor_[0] += (lx * ca + ly * sa) * scale;
                cursor_[2] += (ly * ca - lx * sa) * scale;
                moved = true;
            }
            // the D-pad steps a whole cell (auto repeat 20 / 5)
            const uint32_t dir = bits & (kDUp | kDRight | kDDown | kDLeft);
            bool fire = false;
            if (!dir) edit_hold_ = 0;
            else if (pressed & (kDUp | kDRight | kDDown | kDLeft)) { fire = true; edit_hold_ = 1; }
            else { if (edit_hold_ >= 20 && (edit_hold_ - 20) % 5 == 0) fire = true; ++edit_hold_; }
            if (fire) {
                const int area = edit_area_ >= 0 ? edit_area_ : dccore::edit_find_area(*town_, cursor_);
                const float unit = area >= 0 ? town_->areas[size_t(area)].def.unit : (town_->areas.empty() ? 20.0f : town_->areas[0].def.unit);
                float vx = 0, vy = 0;
                if (dir & kDRight) vx += unit;
                if (dir & kDLeft) vx -= unit;
                if (dir & kDDown) vy += unit;
                if (dir & kDUp) vy -= unit;
                const float target[3] = {cursor_[0] + vx * ca + vy * sa, cursor_[1], cursor_[2] + vy * ca - vx * sa};
                const int ta = dccore::edit_find_area(*town_, target);
                if (ta >= 0) {
                    int cx, cz;
                    dccore::edit_cell(town_->areas[size_t(ta)], target, cx, cz);
                    const auto c = dccore::edit_cell_center(town_->areas[size_t(ta)], cx, cz);
                    cursor_[0] = c[0];
                    cursor_[2] = c[2];
                    moved = true;
                }
            }
            if (moved) edit_limit_cursor(prev);
            else edit_limit_cursor(cursor_);
            // turning the part to place
            if (edit_status_ == 0 || edit_status_ == 1) {
                if (btn_down & kBitL2) { if (++edit_rot_ > 2) edit_rot_ = -1; }
                if (btn_down & kBitR2) { if (--edit_rot_ < -1) edit_rot_ = 2; }
            }
            // place / pick up / put away
            if ((btn_down & dccore::kPadOK) && drops_.empty()) {
                if ((edit_status_ == 0 || edit_status_ == 1) && edit_def_ >= 0) {
                    std::string why;
                    const int slot = edit_valid_ ? geo_->place(edit_def_, edit_rot_, cursor_, &why) : -1;
                    if (slot >= 0) {
                        edit_notice("place", edit_def_);
                        apply_edit_log();
                        rebuild_after_edit();
                        if (edit_status_ == 1 || geo_->placed(edit_def_) >= geo_->max_copies(edit_def_)) { edit_status_ = 3; edit_def_ = -1; }
                    } else {
                        edit_notice("denied");
                    }
                } else if (edit_status_ == 3) {
                    int rot = 0;
                    const int part = geo_->remove_at(cursor_, &rot);
                    if (part >= 0) {
                        edit_def_ = part;
                        edit_rot_ = rot;
                        edit_status_ = 1;
                        edit_notice("remove", part);
                        apply_edit_log();
                        rebuild_after_edit();
                    } else {
                        edit_notice("denied");
                    }
                }
            } else if ((btn_down & dccore::kPadUse) && edit_status_ == 3) {
                int rot = 0;
                const int part = geo_->remove_at(cursor_, &rot);
                if (part >= 0) {
                    edit_notice("remove", part);
                    apply_edit_log();
                    rebuild_after_edit();
                } else {
                    edit_notice("denied");
                }
            }
        }
    }
    edit_rest_ = moved ? 0 : edit_rest_ + 1;
    edit_cam_.follow[0] = cursor_[0];
    edit_cam_.follow[1] = cursor_[1];
    edit_cam_.follow[2] = cursor_[2];
    edit_cam_.speed = edit_frames_ <= 30 ? 10.0f : 8.0f;
    dccore::camera_step(edit_cam_, 1);
    update_ghost(edit_menu_request_);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// the preview of the part to place and the outline of the cells

void DcTownWalk::update_ghost(bool hide) {
    dccore::Town &t = *town_;
    const int def = (!hide && (edit_status_ == 0 || edit_status_ == 1) && edit_def_ >= 0) ? edit_def_ : -1;
    const int area = edit_area_;
    int cx = 0, cz = 0;
    if (area >= 0) dccore::edit_cell(t.areas[size_t(area)], cursor_, cx, cz);
    struct Cell { float x, y, z; Color c; };
    std::vector<Cell> cells;
    float target[3] = {cursor_[0], cursor_[1], cursor_[2]};
    edit_valid_ = false;
    if (def >= 0 && area >= 0) {
        edit_valid_ = geo_->placed(def) < geo_->max_copies(def) && dccore::edit_check_parts(t, def, edit_rot_, area, cx, cz);
        const auto p = dccore::edit_part_position(t, area, cx, cz, def, edit_rot_);
        target[0] = p[0]; target[1] = p[1]; target[2] = p[2];
        const Color col = edit_valid_ ? Color(0.2f, 1.0f, 0.3f, 0.3f) : Color(1.0f, 0.2f, 0.2f, 0.3f);
        for (const auto &c : dccore::edit_footprint_cells(t, def, edit_rot_)) {
            if (c[2] == 0) continue;
            const int gx = cx + c[0], gz = cz + c[1];
            if (gx < 0 || gz < 0 || gx >= t.areas[size_t(area)].def.width || gz >= t.areas[size_t(area)].def.height) continue;
            const auto cc = dccore::edit_cell_center(t.areas[size_t(area)], gx, gz);
            cells.push_back({cc[0], cc[1], cc[2], col});
        }
    } else if (!hide && area >= 0) {
        const int slot = dccore::edit_slot_at(t, cursor_);
        if (edit_status_ == 3 && slot >= 0) {   // the part under the cursor is marked
            const dccore::EditSlot &s = t.slots[size_t(slot)];
            for (const auto &c : dccore::edit_footprint_cells(t, s.def, s.rot)) {
                if (c[2] == 0) continue;
                const int gx = s.cx + c[0], gz = s.cz + c[1];
                const dccore::Town::Area &a = t.areas[size_t(s.area)];
                if (gx < 0 || gz < 0 || gx >= a.def.width || gz >= a.def.height) continue;
                const auto cc = dccore::edit_cell_center(a, gx, gz);
                cells.push_back({cc[0], cc[1], cc[2], Color(1.0f, 0.9f, 0.2f, 0.3f)});
            }
        } else {
            const auto cc = dccore::edit_cell_center(t.areas[size_t(area)], cx, cz);
            cells.push_back({cc[0], cc[1], cc[2], Color(1.0f, 1.0f, 1.0f, 0.35f)});
        }
    }
    // outline mesh
    if (!edit_outline_) {
        edit_outline_ = memnew(MeshInstance3D);
        add_child(edit_outline_);
    }
    std::vector<float> sig;
    for (const Cell &c : cells) { sig.push_back(c.x); sig.push_back(c.z); sig.push_back(c.c.r); sig.push_back(c.c.g); }
    if (sig != outline_sig_) {
        outline_sig_ = sig;
        Ref<ArrayMesh> mesh;
        mesh.instantiate();
        if (!cells.empty() && area >= 0) {
            const float half = t.areas[size_t(area)].def.unit * 0.5f - 0.5f;
            PackedVector3Array v;
            PackedColorArray col;
            PackedInt32Array idx;
            for (const Cell &c : cells) {
                const int base = int(v.size());
                const float y = c.y + 1.5f;
                v.push_back(Vector3(c.x - half, y, c.z - half));
                v.push_back(Vector3(c.x + half, y, c.z - half));
                v.push_back(Vector3(c.x + half, y, c.z + half));
                v.push_back(Vector3(c.x - half, y, c.z + half));
                for (int k = 0; k < 4; ++k) col.push_back(c.c);
                for (int k : {0, 2, 1, 0, 3, 2}) idx.push_back(base + k);
            }
            Array arrays;
            arrays.resize(Mesh::ARRAY_MAX);
            arrays[Mesh::ARRAY_VERTEX] = v;
            arrays[Mesh::ARRAY_COLOR] = col;
            arrays[Mesh::ARRAY_INDEX] = idx;
            mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
            Ref<StandardMaterial3D> m;
            m.instantiate();
            m->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
            m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
            m->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
            m->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
            mesh->surface_set_material(0, m);
        }
        edit_outline_->set_mesh(mesh);
    }
    // the model of the part, floating over the target cell
    if (ghost_def_ != def) {
        if (edit_ghost_) { remove_child(edit_ghost_); edit_ghost_->queue_free(); edit_ghost_ = nullptr; }
        ghost_def_ = def;
        if (def >= 0) {
            auto it = t.info.parts.find(def);
            const int asset = it != t.info.parts.end() && !it->second.file.empty() ? dccore::town_asset(t, it->second.file) : -1;
            if (asset >= 0) {
                edit_ghost_ = scene_->make_ghost(asset);
                if (edit_ghost_) {
                    add_child(edit_ghost_);
                    ghost_pos_[0] = target[0]; ghost_pos_[1] = target[1] + 55.0f; ghost_pos_[2] = target[2];
                    ghost_yaw_ = kPi * float(edit_rot_) * 0.5f;
                }
            }
        }
    }
    if (edit_ghost_) {
        const float goal[3] = {target[0], target[1] + 55.0f, target[2]};
        float d2 = 0;
        for (int k = 0; k < 3; ++k) d2 += (goal[k] - ghost_pos_[k]) * (goal[k] - ghost_pos_[k]);
        if (d2 < 1.0f) for (int k = 0; k < 3; ++k) ghost_pos_[k] = goal[k];
        else for (int k = 0; k < 3; ++k) ghost_pos_[k] += (goal[k] - ghost_pos_[k]) / 8.0f;
        const float want = kPi * float(edit_rot_) * 0.5f;
        float diff = std::fmod(want - ghost_yaw_ + kPi, 2.0f * kPi);
        if (diff < 0) diff += 2.0f * kPi;
        diff -= kPi;
        ghost_yaw_ = std::fabs(diff) < 0.01f ? want : ghost_yaw_ + diff * 0.2f;
        edit_ghost_->set_basis(Basis(Vector3(0, 1, 0), ghost_yaw_));
        edit_ghost_->set_position(Vector3(ghost_pos_[0], ghost_pos_[1], ghost_pos_[2]));
        if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(edit_ghost_)) {
            if (!edit_valid_) {
                Ref<StandardMaterial3D> red;
                red.instantiate();
                red->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
                red->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                red->set_albedo(Color(1.0f, 0.1f, 0.1f, 0.55f));
                mi->set_material_overlay(red);
            } else {
                mi->set_material_overlay(Ref<Material>());
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// following the data into the scene

void DcTownWalk::apply_edit_log() {
    dccore::Town &t = *town_;
    for (const dccore::EditChange &c : t.edit_log) {
        switch (c.kind) {
            case dccore::EditChange::Add:
                scene_->add_instance(c.instance);
                if (c.instance >= 0 && size_t(c.instance) < t.instances.size()) {
                    auto it = t.info.parts.find(t.instances[size_t(c.instance)].part_def);
                    if (it != t.info.parts.end() && dccore::edit_part_kind(it->second) == 0) {   // buildings drop into place
                        drops_.push_back({c.instance, 15});
                        scene_->set_instance_offset(c.instance, 50.0f);
                    }
                }
                break;
            case dccore::EditChange::Update: scene_->refresh_instance(c.instance); break;
            case dccore::EditChange::Remove: scene_->remove_instance(c.instance); break;
        }
    }
    t.edit_log.clear();
    geo_->request_check();
    apply_element_frames();
    geo_->save_layout();
}

// the villagers live where their houses stand; the scripts address placed parts by instance
void DcTownWalk::rebuild_after_edit() {
    placed_instances_.clear();
    for (size_t i = 0; i < town_->instances.size(); ++i)
        if (town_->instances[i].part_def >= 0 && !town_->instances[i].removed) placed_instances_.push_back(int(i));
    near_event_ = -1;
    near_person_ = -1;
    for (DcCharacter *v : villager_nodes_) if (v) { remove_child(v); v->queue_free(); }
    villager_nodes_.clear();
    make_villagers();
}

// EdPartsObjectOnOff: the frames of the elements that are (not) installed
void DcTownWalk::apply_element_frames() {
    if (!geo_ || town_->interior) return;
    for (size_t i = 0; i < town_->instances.size(); ++i) {
        const dccore::TownInstance &in = town_->instances[i];
        if (in.part_def < 0 || in.part_def >= 24 || in.removed) continue;
        for (const auto &f : geo_->element_frames(in.part_def)) scene_->set_frame_visible(int(i), String(f.first.c_str()), f.second);
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// what the 2D layer shows

Dictionary DcTownWalk::get_edit_ui() {
    Dictionary d;
    d["can_edit"] = can_edit();
    d["active"] = mode_ == ModeEdit;
    if (!geo_ || mode_ != ModeEdit) return d;
    d["status"] = edit_status_;
    d["part"] = edit_def_;
    d["part_name"] = edit_def_ >= 0 ? part_name(edit_def_) : String();
    d["rot"] = edit_rot_;
    d["valid"] = edit_valid_;
    d["menu"] = edit_menu_request_;
    d["board_first"] = edit_status_ == 0;
    float p[3];
    d["can_leave"] = drops_.empty() && edit_check_to_walk(p);
    d["cursor"] = Vector3(cursor_[0], cursor_[1], cursor_[2]);
    if (camera_ && !camera_->is_position_behind(Vector3(cursor_[0], cursor_[1], cursor_[2]))) d["screen"] = camera_->unproject_position(Vector3(cursor_[0], cursor_[1], cursor_[2]));
    if (edit_def_ >= 0) { d["placed"] = geo_->placed(edit_def_); d["max"] = geo_->max_copies(edit_def_); }
    String over;
    if (edit_rest_ >= 30 && edit_frames_ >= 30 && !edit_menu_request_) {
        const int slot = dccore::edit_slot_at(*town_, cursor_);
        if (slot >= 0) over = part_name(town_->slots[size_t(slot)].def);
    }
    d["over_name"] = over;
    d["requests_met"] = geo_->request_count();
    d["requests_total"] = geo_->request_total();
    return d;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// the Atla board

Array DcTownWalk::georama_board() {
    Array pages;
    if (!geo_ || !env_.geo_tables) return pages;
    const int t = geo_->town_no();
    for (int p = 0; p < geo_->part_count(); ++p) {
        if (!geo_->usable(p)) continue;
        const dccore::GeoPartDef &def = geo_->def(p);
        Dictionary page;
        page["part"] = p;
        page["name"] = part_name(p);
        page["max"] = geo_->max_copies(p);
        page["placed"] = geo_->placed(p);
        page["complete"] = geo_->complete(p);
        page["tile"] = env_.geo_tables->elem[t][p].tile;
        Array slots;
        for (int k = 0; k < 6; ++k) {
            const dccore::GeoElemDef &e = def.elem[k];
            if (e.id < 0) continue;
            Dictionary s;
            s["k"] = k;
            s["chip"] = e.id;
            s["name"] = chip_name(e.id, false);
            s["desc"] = chip_name(e.id, true);
            s["installed"] = geo_->installed(p, k);
            s["visible"] = e.rel < 0 || geo_->installed(p, e.rel);
            s["tile"] = env_.geo_tables->elem[t][0x28 + e.id].tile;
            slots.push_back(s);
        }
        page["slots"] = slots;
        pages.push_back(page);
    }
    return pages;
}

Array DcTownWalk::georama_tray() {
    Array tray;
    if (!geo_) return tray;
    for (int i = 0; i < 0x80; ++i) tray.push_back(geo_->tray_get(i));
    return tray;
}

bool DcTownWalk::georama_press_slot(int part, int slot) {
    if (!geo_ || part < 0 || part >= 24 || slot < 0 || slot >= 6) return false;
    const dccore::GeoElemDef &e = geo_->def(part).elem[slot];
    if (e.id < 0 || !geo_->usable(part) || geo_->complete(part)) return false;   // a finished house is locked
    if (hand_chip_ < 0) {
        if (!geo_->installed(part, slot)) return false;
        for (int j = 0; j < 6; ++j)   // another installed tip needs this one
            if (j != slot && geo_->def(part).elem[j].id >= 0 && geo_->def(part).elem[j].rel == slot && geo_->installed(part, j)) return false;
        geo_->set_installed(part, slot, false);
        hand_chip_ = e.id;
        hand_part_ = part;
        hand_slot_ = slot;
    } else {
        if (!geo_->install_tip(part, slot, hand_chip_)) return false;
        hand_chip_ = -1;
        hand_part_ = hand_slot_ = -1;
        if (geo_->complete(part)) edit_notice("complete", part);
    }
    tip_changed();
    return true;
}

bool DcTownWalk::georama_press_tray(int cell) {
    if (!geo_ || cell < 0 || cell >= 0x80) return false;
    const int cur = geo_->tray_get(cell);
    if (cur < 0 && hand_chip_ < 0) return false;
    geo_->tray_set(cell, hand_chip_);
    hand_chip_ = cur;
    hand_part_ = hand_slot_ = -1;
    tip_changed();
    return true;
}

void DcTownWalk::georama_cancel_hand() {
    if (!geo_ || hand_chip_ < 0) return;
    if (hand_part_ >= 0) {
        geo_->set_installed(hand_part_, hand_slot_, true);
    } else {
        for (int i = 0; i < 0x80; ++i)
            if (geo_->tray_get(i) < 0) { geo_->tray_set(i, hand_chip_); break; }
    }
    hand_chip_ = -1;
    hand_part_ = hand_slot_ = -1;
    tip_changed();
}

void DcTownWalk::georama_sort_tray() {
    if (!geo_) return;
    std::vector<int> chips;
    for (int i = 0; i < 0x80; ++i) if (geo_->tray_get(i) >= 0) chips.push_back(geo_->tray_get(i));
    std::sort(chips.begin(), chips.end());
    for (int i = 0; i < 0x80; ++i) geo_->tray_set(i, size_t(i) < chips.size() ? chips[size_t(i)] : -1);
}

bool DcTownWalk::georama_select_part(int part) {
    if (!geo_ || hand_chip_ >= 0 || part < 0 || part >= 24 || !geo_->usable(part)) return false;
    if (geo_->placed(part) >= geo_->max_copies(part)) return false;
    edit_def_ = part;
    edit_status_ = 0;
    edit_rot_ = 0;
    edit_picked_ = true;
    return true;
}

Array DcTownWalk::georama_analysis() {
    Array out;
    if (!geo_ || !env_.geo_tables) return out;
    const int n = geo_->part_count();
    int have = 0, total = 0, complete = 0;
    for (int p = 0; p < n; ++p) {
        if (geo_->obtained(p)) ++have;
        ++total;
        for (int k = 0; k < 6; ++k)
            if (geo_->def(p).elem[k].id >= 0) { ++total; if (geo_->installed(p, k)) ++have; }
        if (geo_->obtained(p) && geo_->complete(p)) ++complete;
    }
    for (int i = 0; i < 0x80; ++i) if (geo_->tray_get(i) >= 0) ++have;
    out.push_back(total > 0 ? std::min(100, have * 100 / total) : 0);
    out.push_back(n > 0 ? complete * 100 / n : 0);
    out.push_back(geo_->request_percent());
    return out;
}

// installing or taking a tip changes the look of the houses, the villagers and the wishes
void DcTownWalk::tip_changed() {
    geo_->request_check();
    apply_element_frames();
    geo_->save_layout();
    rebuild_after_edit();
}
