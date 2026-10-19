// Fishing (GameMode 0x10 of the towns): the hero with his rod at the edge of the pond, the float and the hook on a simple line, the fish of the pond, the bite and the
// catch. The state machine is the one of EdMoveChara (notes/fishing.md section 1.5); the rope physics are simplified to a cast along an arc and a polyline.
#include "dc_townwalk.h"

#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/immediate_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <dccore/fishing.h>
#include <dccore/libm_ps2.h>

using namespace godot;

namespace {
constexpr float kPi = 3.14159265358979f;
constexpr int kHookDrop = 8;   // the hook hangs about 8 below the float
}

// ---- entering and leaving ---------------------------------------------------------------------------------------------------------------------------------

void DcTownWalk::fish_enter() {
    if (!env_.fish_tables || !rt_ || !rt_->fishing.loaded) { fish_active_ = false; return; }
    const auto &fs = rt_->fishing;
    const bool first = !fish_active_;
    fish_active_ = true;
    if (first || !fish_sim_) {
        fish_sim_ = std::make_unique<dccore::FishingSim>(env_.fish_tables, env_.rng);
        fish_sim_->water_y = fs.water;
        fish_sim_->ground_y = fs.ground;
        fish_sim_->load(fs.pond_set, rt_->time_bucket());
        fish_sim_->init_area(fs.area[0], fs.area[1], fs.area[2], fs.area[3]);
        fish_bait_item_ = -1;
        fish_sim_->bait = -1;
        fish_idle_ = 0;
    }
    fish_sim_->water_y = fs.water;
    fish_sim_->ground_y = fs.ground;
    // the bait the script attached (the previous one goes back into the bag)
    if (rt_->fishing.bait_changed) fish_set_bait(rt_->fishing.bait_item);
    rt_->fishing.bait_changed = false;
    const dccore::EvActor &h = rt_->actor(-1);
    fish_hero_ = Vector3(h.pos.x, h.pos.y, h.pos.z);
    fish_yaw_ = h.rot.y;
    fish_state_ = 0;
    fish_t_ = 0;
    fish_line_out_ = false;
    mode_ = ModeFish;
    fish_make_nodes();
}

void DcTownWalk::fish_set_bait(int item) {
    if (!env_.status || !env_.items) return;
    if (fish_bait_item_ >= 0 && fish_bait_item_ != item && dccore::check_item_get(*env_.status, fish_bait_item_) == 0)
        dccore::get_item(*env_.status, *env_.items, fish_bait_item_, *env_.rng);   // FishingLoadEsa: the old bait back to the bag
    fish_bait_item_ = item;
    int idx = -1;
    for (int b = 0; b < 13; ++b)
        if (env_.fish_tables && env_.fish_tables->bait_item[b] == item) idx = b;
    if (fish_sim_) fish_sim_->bait = idx;
}

void DcTownWalk::fish_leave() {
    if (!fish_active_) return;
    fish_active_ = false;
    if (fish_bait_item_ >= 0 && env_.status && env_.items && dccore::check_item_get(*env_.status, fish_bait_item_) == 0)
        dccore::get_item(*env_.status, *env_.items, fish_bait_item_, *env_.rng);   // FishingExit: the bait comes back
    fish_bait_item_ = -1;
    fish_sim_.reset();
    for (Node3D *n : fish_nodes_) if (n) { remove_child(n); n->queue_free(); }
    fish_nodes_.clear();
    for (Node3D **p : {&fish_float_node_, &fish_line_node_, &fish_catch_node_})
        if (*p) { remove_child(*p); (*p)->queue_free(); *p = nullptr; }
    if (rt_) rt_->system_message.active = false;
}

void DcTownWalk::fish_make_nodes() {
    if (!fish_float_node_) {
        MeshInstance3D *m = memnew(MeshInstance3D);
        Ref<SphereMesh> sm;
        sm.instantiate();
        sm->set_radius(1.4f);
        sm->set_height(2.8f);
        m->set_mesh(sm);
        Ref<StandardMaterial3D> mat;
        mat.instantiate();
        mat->set_albedo(Color(1.0f, 0.25f, 0.2f));
        mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        m->set_material_override(mat);
        add_child(m);
        fish_float_node_ = m;
    }
    if (!fish_line_node_) {
        MeshInstance3D *m = memnew(MeshInstance3D);
        Ref<ImmediateMesh> im;
        im.instantiate();
        m->set_mesh(im);
        Ref<StandardMaterial3D> mat;
        mat.instantiate();
        mat->set_albedo(Color(0.95f, 0.95f, 0.85f));
        mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
        m->set_material_override(mat);
        add_child(m);
        fish_line_node_ = m;
    }
    if (fish_nodes_.empty() && fish_sim_) {   // the fish under the water: the silhouette model, or a dark ellipse when it cannot be read
        std::shared_ptr<dccore::Character> sil;
        if (auto p = pack_at("chara/f00s.chr")) {
            try { sil = std::make_shared<dccore::Character>(dccore::load_character(*p)); } catch (const std::exception &) {}
        }
        for (int i = 0; i < fish_sim_->fish_num; ++i) {
            Node3D *n = nullptr;
            if (sil) {
                DcCharacter *c = make_actor_node(sil);
                c->play(6, true);
                n = c;
            } else {
                MeshInstance3D *m = memnew(MeshInstance3D);
                Ref<SphereMesh> sm;
                sm.instantiate();
                m->set_mesh(sm);
                Ref<StandardMaterial3D> mat;
                mat.instantiate();
                mat->set_albedo(Color(0.05f, 0.08f, 0.1f, 0.8f));
                mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
                m->set_material_override(mat);
                add_child(m);
                n = m;
            }
            fish_nodes_.push_back(n);
        }
    }
}

// ---- the rod -----------------------------------------------------------------------------------------------------------------------------------------------

Vector3 DcTownWalk::fish_rod_tip() const {
    DcCharacter *hero = actor_nodes_.count(-1) ? actor_nodes_.at(-1) : player_;
    if (hero) {
        const int joint = hero->find_joint("sao");
        if (joint >= 0) {
            dccore::Mat4 id{};
            id[0] = id[5] = id[10] = id[15] = 1.0;
            return hero->get_global_transform().xform(hero->attach_transform(joint, id).origin);
        }
    }
    return fish_hero_ + Vector3(std::sin(fish_yaw_) * 8.0f, 24.0f, std::cos(fish_yaw_) * 8.0f);
}

void DcTownWalk::fish_motion(int motion, bool loop) {
    DcCharacter *hero = actor_nodes_.count(-1) ? actor_nodes_.at(-1) : player_;
    fish_motion_now_ = motion;
    fish_motion_ticks_ = 0;
    if (!hero) return;
    hero->play(motion, loop);
    const Dictionary mi = hero->get_motion_info(motion);
    if (mi.has("end")) {
        const double speed = mi.has("speed") && double(mi["speed"]) > 0.0 ? double(mi["speed"]) : 1.0;
        fish_motion_ticks_ = int(std::ceil((double(mi["end"]) - double(mi["start"])) / speed));
    }
}

// ---- one tick ----------------------------------------------------------------------------------------------------------------------------------------------

void DcTownWalk::step_fish(const dccore::PadRaw &pad, bool ok_down, uint32_t menu_pressed) {
    if (!fish_sim_) { mode_ = ModeWalk; return; }
    if (fish_auto_ && ((fish_state_ == 1 && fish_idle_ > 20 && fish_bait_item_ >= 0) || fish_state_ == 7 || (fish_state_ == 11 && fish_t_ > 80))) ok_down = true;
    const bool circle_down = (menu_pressed & 0x20) != 0 || (pad.buttons & dccore::kPadNO) != 0 && !fish_prev_no_;
    fish_prev_no_ = (pad.buttons & dccore::kPadNO) != 0;
    const bool square_down = (pad.buttons & dccore::kPadUse) != 0 && !fish_prev_use_;
    fish_prev_use_ = (pad.buttons & dccore::kPadUse) != 0;
    const float lx = dccore::pad_axis(pad.lx);
    ++fish_t_;
    --fish_motion_ticks_;
    const float water = fish_sim_->water_y;
    const Vector3 tip = fish_rod_tip();
    auto hero_dir = [&]() { return Vector3(std::sin(fish_yaw_), 0, std::cos(fish_yaw_)); };
    const Vector3 target = fish_hero_ + hero_dir() * 48.0f + Vector3(0, water - fish_hero_.y, 0);
    auto say = [&](const std::string &t) { if (rt_) { rt_->system_message.active = true; rt_->system_message.text = t; rt_->system_message_ticks = 0; rt_->in_event_message = true; } };
    auto bait_name = [&]() -> std::string {
        if (fish_bait_item_ < 0) return std::string();
        return env_.system_text && env_.items ? env_.system_text->get(env_.items->name_message(fish_bait_item_)) : std::string();
    };
    switch (fish_state_) {
        case 0:   // FishingInitFishStatus
            fish_sim_->release_all();
            fish_idle_ = 0;
            fish_float_ = tip;
            fish_state_ = 1;
            fish_motion(0, true);
            break;
        case 1: {   // idle: the rod is ready
            fish_float_ = tip;
            fish_line_out_ = false;
            if (++fish_idle_ > 0x78) say(std::string("Cast / Change Bait / Quit : ") + (fish_bait_item_ >= 0 ? bait_name() : "No hook"));
            if (ok_down) { fish_state_ = 2; fish_idle_ = 0; if (rt_) rt_->system_message.active = false; }
            else if (square_down) { fish_pending_event_ = 134; }
            else if (circle_down) { fish_pending_event_ = 133; }
            break;
        }
        case 2:   // the cast starts
            fish_motion(4, false);
            fish_cast_t_ = 0;
            fish_state_ = 3;
            break;
        case 3: {   // the cast: the float flies along an arc to the water
            ++fish_cast_t_;
            const float k = std::min(1.0f, float(fish_cast_t_) / 90.0f);
            if (fish_cast_t_ > 40) {   // (the rod swings first)
                const float u = std::min(1.0f, float(fish_cast_t_ - 40) / 40.0f);
                fish_float_ = tip.lerp(target, u) + Vector3(0, 14.0f * std::sin(u * kPi), 0);
            }
            (void)k;
            fish_line_out_ = true;
            if (fish_cast_t_ >= 90 && fish_sound) fish_sound(400);
            if (fish_motion_ticks_ <= 0 || fish_cast_t_ > 150) { fish_state_ = 4; fish_t_ = 0; fish_motion(5, true); fish_float_ = target; }
            break;
        }
        case 4:
        case 6:
        case 7: {   // the line is out: the float rests, the fish look at the hook
            fish_line_out_ = true;
            const float sway = fish_state_ == 4 ? 0.0f : 1.0f;
            const float turn = -lx * 0.1f;
            fish_yaw_ = dccore::angle_interpolate(fish_yaw_, fish_yaw_ + turn, 0.01f, 0);
            Vector3 want = fish_hero_ + hero_dir() * 48.0f + Vector3(0, water - fish_hero_.y, 0);
            fish_float_ = fish_float_.lerp(want, 0.08f);
            float bob = 0.0f;
            if (fish_state_ == 6) bob = -0.15f * fish_sim_->rnd() * 6.0f * sway;
            if (fish_state_ == 7) bob = -(0.1f + 0.05f * fish_sim_->rnd()) * 12.0f;
            fish_float_.y = water + bob;
            Vector3 hook = fish_float_ + Vector3(0, -float(kHookDrop) + bob, 0);
            const float h3[3] = {hook.x, hook.y, hook.z};
            fish_sim_->step(h3);
            int idx = -1;
            const int st = fish_sim_->status(&idx);
            const bool out_of_pond = fish_float_.x < std::min(rt_->fishing.rect[0], rt_->fishing.rect[2]) - 20.0f || fish_float_.x > std::max(rt_->fishing.rect[0], rt_->fishing.rect[2]) + 20.0f ||
                                     fish_float_.z < std::min(rt_->fishing.rect[1], rt_->fishing.rect[3]) - 20.0f || fish_float_.z > std::max(rt_->fishing.rect[1], rt_->fishing.rect[3]) + 20.0f;
            if (fish_state_ == 4) {
                if (st == 7) fish_state_ = 6;
                if (st == 8) { fish_state_ = 7; fish_hooked_ = idx; fish_hook_window_ = 0; }
                if (ok_down || (fish_t_ > 30 && out_of_pond)) { fish_state_ = 5; fish_t_ = 0; fish_motion(7, false); }
            } else if (fish_state_ == 6) {
                if (st == 8) { fish_state_ = 7; fish_hooked_ = idx; fish_hook_window_ = 0; }
                else if (st != 7) fish_state_ = 4;
                if (ok_down) {   // too early
                    fish_state_ = 9;
                    fish_t_ = 0;
                    fish_motion(7, false);
                    if (fish_sound) fish_sound(402);
                    if (fish_sim_->rnd() < 0.2f && fish_bait_item_ >= 0) { fish_bait_item_ = -1; fish_sim_->bait = -1; say("Bait ran out."); }
                }
            } else {   // state 7: the fish is on the hook for a short while
                ++fish_hook_window_;
                if (ok_down) {
                    fish_state_ = 10;
                    fish_t_ = 0;
                    fish_catch_index_ = fish_hooked_;
                    fish_motion(12, true);
                    if (fish_sound) fish_sound(400);
                    fish_bait_item_ = -1;   // the bait is used up
                    fish_sim_->bait = -1;
                    for (int i = 0; i < fish_sim_->fish_num; ++i) if (i != fish_hooked_) fish_sim_->fish[size_t(i)].bite = -1;
                    fish_sim_->fish[size_t(fish_hooked_)].move_mode = 0xb;
                } else if (st != 8) {
                    fish_state_ = 4;
                    if (fish_sim_->rnd() < 0.3f && fish_bait_item_ >= 0) { fish_bait_item_ = -1; fish_sim_->bait = -1; say("Bait ran out."); }
                }
            }
            break;
        }
        case 5:   // reeling in: the float comes back to the rod
        case 9: {
            const float u = std::min(1.0f, float(fish_t_) / 40.0f);
            fish_float_ = fish_float_.lerp(tip, 0.06f + 0.04f * u);
            if (fish_t_ >= 30 && fish_sound && fish_t_ == 30) fish_sound(402);
            if (fish_motion_ticks_ <= 0 || fish_t_ > 80) { fish_state_ = fish_state_ == 5 ? 1 : 1; fish_idle_ = 0; fish_motion(0, true); fish_sim_->release_all(); }
            break;
        }
        case 10: {   // the fight: a fixed scene, the catch is certain
            Vector3 hook = fish_float_ + Vector3(0, -float(kHookDrop), 0);
            fish_float_ = fish_float_.lerp(tip + Vector3(0, -2, 0), 0.01f);
            (void)hook;
            const float h3[3] = {fish_float_.x, fish_float_.y - 8.0f, fish_float_.z};
            dccore::Fish &f = fish_sim_->fish[size_t(std::max(0, fish_catch_index_))];
            f.pos[0] = h3[0] + 4.0f * std::sin(float(fish_t_) * 0.3f);
            f.pos[2] = h3[2] + 4.0f * std::cos(float(fish_t_) * 0.3f);
            if (fish_t_ >= 0x78) {
                const dccore::FishCatch c = fish_sim_->catch_of(fish_catch_index_);
                fish_last_catch_ = c;
                if (env_.save) dccore::register_catch(*env_.save, c);
                if (fish_sound) fish_sound(403);
                std::string name = env_.system_text ? env_.system_text->get(env_.fish_tables->fish[std::clamp(c.kind, 0, 17)].message + 30) : std::string("Fish");
                char buf[256];
                std::snprintf(buf, sizeof buf, "%s (%.1fcm) caught!!  Fishing Points +%d  Total %d", name.c_str(), float(c.size10) / 10.0f, c.points, env_.save ? dccore::fishing_points(*env_.save) : 0);
                fish_catch_text_ = buf;
                say(fish_catch_text_);
                // the fish on the line: its own model
                if (auto p = pack_at(dccore::FishTables::model_file(c.kind))) {
                    try {
                        auto ch = std::make_shared<dccore::Character>(dccore::load_character(*p));
                        if (fish_catch_node_) { remove_child(fish_catch_node_); fish_catch_node_->queue_free(); }
                        DcCharacter *node = make_actor_node(ch);
                        node->play(1, true);
                        node->set_scale(Vector3(1, 1, 1) * f.scale);
                        fish_catch_node_ = node;
                    } catch (const std::exception &) {}
                }
                fish_state_ = 11;
                fish_t_ = 0;
                fish_motion(9, false);
            }
            break;
        }
        case 11: {   // the catch is shown until a button is pressed
            fish_float_ = tip + Vector3(0, -6, 0);
            if (fish_catch_node_) {
                fish_catch_node_->set_position(fish_float_ + Vector3(0, -4, 0));
                fish_catch_node_->set_rotation(Vector3(0, fish_yaw_ + kPi, 0));
            }
            if (fish_t_ > 60 && (ok_down || circle_down || fish_t_ > 600)) {
                if (fish_sound) fish_sound(404);
                if (fish_catch_node_) { remove_child(fish_catch_node_); fish_catch_node_->queue_free(); fish_catch_node_ = nullptr; }
                fish_sim_->release_all();
                fish_sim_->fish[size_t(std::max(0, fish_catch_index_))].pos[0] = (rt_->fishing.area[0] + rt_->fishing.area[2]) * 0.5f;
                fish_sim_->fish[size_t(std::max(0, fish_catch_index_))].pos[2] = (rt_->fishing.area[1] + rt_->fishing.area[3]) * 0.5f;
                fish_sim_->fish[size_t(std::max(0, fish_catch_index_))].move_mode = 0;
                if (rt_) rt_->system_message.active = false;
                fish_state_ = 1;
                fish_idle_ = 0;
                fish_motion(0, true);
            }
            break;
        }
        default: break;
    }
    // the events of the rod: change the bait (Square) and the menu of Circle
    if (fish_pending_event_ >= 0 && rt_) {
        const int ev = fish_pending_event_;
        fish_pending_event_ = -1;
        if (rt_->has_function(ev)) {
            if (rt_) rt_->system_message.active = false;
            fish_state_ = 1;
            start_event(ev);
        }
    }
}

// ---- drawing -----------------------------------------------------------------------------------------------------------------------------------------------

void DcTownWalk::fish_apply() {
    if (!fish_active_ || !fish_sim_) return;
    DcCharacter *hero = actor_nodes_.count(-1) ? actor_nodes_[-1] : player_;
    if (hero && mode_ == ModeFish) {
        hero->set_visible(true);
        hero->set_position(fish_hero_);
        hero->set_rotation(Vector3(0, fish_yaw_, 0));
    }
    if (mode_ != ModeFish) return;
    const bool show_line = fish_line_out_ || fish_state_ == 10 || fish_state_ == 11;
    if (fish_float_node_) {
        fish_float_node_->set_visible(show_line || fish_state_ == 5 || fish_state_ == 9);
        fish_float_node_->set_position(fish_float_);
    }
    if (fish_line_node_) {
        Ref<ImmediateMesh> im = Object::cast_to<MeshInstance3D>(fish_line_node_)->get_mesh();
        im->clear_surfaces();
        if (show_line || fish_state_ == 5 || fish_state_ == 9) {
            const Vector3 tip = fish_rod_tip();
            const Vector3 hook = fish_float_ + Vector3(0, -float(kHookDrop), 0);
            im->surface_begin(Mesh::PRIMITIVE_LINE_STRIP);
            const int n = 24;
            for (int i = 0; i <= n; ++i) {   // a hanging curve from the tip to the float, then down to the hook
                const float u = float(i) / float(n);
                Vector3 p = tip.lerp(fish_float_, u);
                p.y -= 6.0f * std::sin(u * kPi) * (fish_line_out_ ? 1.0f : 0.3f);
                im->surface_add_vertex(p);
            }
            im->surface_add_vertex(hook);
            im->surface_end();
        }
    }
    for (size_t i = 0; i < fish_nodes_.size() && i < size_t(fish_sim_->fish_num); ++i) {
        const dccore::Fish &f = fish_sim_->fish[i];
        Node3D *n = fish_nodes_[i];
        const bool caught = int(i) == fish_catch_index_ && (fish_state_ == 10 || fish_state_ == 11);
        n->set_visible(!(rt_ && rt_->fishing.pond_set == 3) && !(caught && fish_state_ == 11));   // (the story pond's fish are not drawn; the caught one is shown by its own model)
        n->set_position(Vector3(f.pos[0], f.pos[1], f.pos[2]));
        n->set_rotation(Vector3(0, f.yaw, 0));
        if (DcCharacter *c = Object::cast_to<DcCharacter>(n)) c->set_scale(Vector3(1, 1, 1) * std::max(0.3f, f.scale));
        else n->set_scale(Vector3(f.len * 0.5f, f.len * 0.15f, f.len * 0.25f));
    }
    if (fish_catch_node_ && fish_state_ == 10) fish_catch_node_->set_visible(false);
}

Vector3 DcTownWalk::fish_camera_pos() const {
    const Vector3 mid = (fish_hero_ + fish_float_) * 0.5f;
    return fish_hero_ + Vector3(0, 40.0f, 0) - Vector3(std::sin(fish_yaw_), 0, std::cos(fish_yaw_)) * 55.0f + (mid - fish_hero_) * 0.2f;
}

Vector3 DcTownWalk::fish_camera_ref() const {
    return (fish_hero_ + Vector3(0, 12, 0) + fish_float_) * 0.5f;
}
