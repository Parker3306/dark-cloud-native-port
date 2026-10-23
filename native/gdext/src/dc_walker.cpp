#include "dc_walker.h"
#include "dc_pad.h"
#include "dc_eb_ui.h"
#include "dccore/weapon_ops.h"
#include <cstdio>
#include <cstdlib>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

#include <algorithm>
#include <cmath>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/classes/input_event_key.hpp>

using namespace godot;

void DcWalker::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_script_ticks", "ticks"), &DcWalker::set_script);
    ClassDB::bind_method(D_METHOD("set_use_devices", "on"), &DcWalker::set_use_devices);
    ClassDB::bind_method(D_METHOD("get_tick_count"), &DcWalker::get_tick_count);
    ClassDB::bind_method(D_METHOD("get_player_position"), &DcWalker::get_player_position);
    ClassDB::bind_method(D_METHOD("get_camera_state"), &DcWalker::get_camera_state);
    ClassDB::bind_method(D_METHOD("get_camera"), &DcWalker::get_camera);
    ClassDB::bind_method(D_METHOD("get_floor"), &DcWalker::get_floor);
    ClassDB::bind_method(D_METHOD("step_ticks", "n"), &DcWalker::step_ticks);
    ClassDB::bind_method(D_METHOD("teleport", "x", "z", "y"), &DcWalker::teleport, DEFVAL(30.0));
    ClassDB::bind_method(D_METHOD("get_monster_info"), &DcWalker::get_monster_info);
    ClassDB::bind_method(D_METHOD("get_player_status"), &DcWalker::get_player_status);
    ClassDB::bind_method(D_METHOD("take_notices"), &DcWalker::take_notices);
    ClassDB::bind_method(D_METHOD("get_minimap"), &DcWalker::get_minimap);
    ClassDB::bind_method(D_METHOD("take_hit_values"), &DcWalker::take_hit_values);
    ClassDB::bind_method(D_METHOD("get_boss_fade"), &DcWalker::get_boss_fade);
    ClassDB::bind_method(D_METHOD("get_event_ui"), &DcWalker::get_event_ui);
    ClassDB::bind_method(D_METHOD("is_view_mode"), &DcWalker::is_view_mode);
    ClassDB::bind_method(D_METHOD("debug_trace", "on"), &DcWalker::debug_trace);
    ClassDB::bind_method(D_METHOD("debug_floor_scan", "x0", "x1", "z0", "z1", "step", "from_y"), &DcWalker::debug_floor_scan);
    ClassDB::bind_method(D_METHOD("debug_log"), &DcWalker::debug_log);
    ClassDB::bind_method(D_METHOD("debug_boss_down"), &DcWalker::debug_boss_down);
    ClassDB::bind_method(D_METHOD("debug_full_life"), &DcWalker::debug_full_life);
    ClassDB::bind_method(D_METHOD("debug_kill_monsters"), &DcWalker::debug_kill_monsters);
    ClassDB::bind_method(D_METHOD("debug_set_monster_hp", "slot", "hp"), &DcWalker::debug_set_monster_hp);
    ClassDB::bind_method(D_METHOD("is_exit_requested"), &DcWalker::is_exit_requested);
    ClassDB::bind_method(D_METHOD("get_event_info"), &DcWalker::get_event_info);
    ClassDB::bind_method(D_METHOD("get_bag"), &DcWalker::get_bag);
    ClassDB::bind_method(D_METHOD("use_bag_item", "slot"), &DcWalker::use_bag_item);
    ClassDB::bind_method(D_METHOD("assign_quick", "quick_slot", "bag_slot"), &DcWalker::assign_quick);
    ClassDB::bind_method(D_METHOD("get_weapons"), &DcWalker::get_weapons);
    ClassDB::bind_method(D_METHOD("equip_weapon", "slot"), &DcWalker::equip_weapon);
    ClassDB::bind_method(D_METHOD("get_character"), &DcWalker::get_character);
    ClassDB::bind_method(D_METHOD("get_hero_pose"), &DcWalker::get_hero_pose);
    ClassDB::bind_method(D_METHOD("get_eb_ui"), &DcWalker::get_eb_ui);
    ClassDB::bind_method(D_METHOD("get_eb_log"), &DcWalker::get_eb_log);
    ClassDB::bind_method(D_METHOD("debug_run_function", "id"), &DcWalker::debug_run_function);
    ClassDB::bind_method(D_METHOD("debug_eb_autoplay", "offset"), &DcWalker::debug_eb_autoplay);
    ClassDB::bind_method(D_METHOD("coop_set_role", "role"), &DcWalker::coop_set_role);
    ClassDB::bind_method(D_METHOD("set_input_blocked", "blocked"), &DcWalker::set_input_blocked);
    ClassDB::bind_method(D_METHOD("drop_bag_item", "slot"), &DcWalker::drop_bag_item);
    ClassDB::bind_method(D_METHOD("coop_spawn_drop", "pos", "item"), &DcWalker::coop_spawn_drop);
    ClassDB::bind_method(D_METHOD("coop_add_remote", "status"), &DcWalker::coop_add_remote);
    ClassDB::bind_method(D_METHOD("coop_remove_remote", "id"), &DcWalker::coop_remove_remote);
    ClassDB::bind_method(D_METHOD("coop_update_remote", "id", "pose"), &DcWalker::coop_update_remote);
    ClassDB::bind_method(D_METHOD("coop_set_remote_status", "id", "status"), &DcWalker::coop_set_remote_status);
    ClassDB::bind_method(D_METHOD("coop_import_spheres", "source", "remote_id", "spheres"), &DcWalker::coop_import_spheres);
    ClassDB::bind_method(D_METHOD("coop_export_spheres"), &DcWalker::coop_export_spheres);
    ClassDB::bind_method(D_METHOD("coop_export_fx"), &DcWalker::coop_export_fx);
    ClassDB::bind_method(D_METHOD("coop_apply_fx", "peer", "data"), &DcWalker::coop_apply_fx);
    ClassDB::bind_method(D_METHOD("coop_clear_fx", "peer"), &DcWalker::coop_clear_fx);
    ClassDB::bind_method(D_METHOD("coop_take_patches"), &DcWalker::coop_take_patches);
    ClassDB::bind_method(D_METHOD("coop_snapshot"), &DcWalker::coop_snapshot);
    ClassDB::bind_method(D_METHOD("coop_apply_snapshot", "snapshot"), &DcWalker::coop_apply_snapshot);
    ClassDB::bind_method(D_METHOD("coop_apply_patch", "offset", "bytes"), &DcWalker::coop_apply_patch);
    ClassDB::bind_method(D_METHOD("coop_status_bytes"), &DcWalker::coop_status_bytes);
    ClassDB::bind_method(D_METHOD("coop_take_hit_values"), &DcWalker::coop_take_hit_values);
    ClassDB::bind_method(D_METHOD("coop_take_remote_notices"), &DcWalker::coop_take_remote_notices);
    ClassDB::bind_method(D_METHOD("coop_show_hits", "hits"), &DcWalker::coop_show_hits);
    ClassDB::bind_method(D_METHOD("coop_show_notice", "notice"), &DcWalker::coop_show_notice);
    ClassDB::bind_method(D_METHOD("coop_take_sounds"), &DcWalker::coop_take_sounds);
    ClassDB::bind_method(D_METHOD("coop_play_sounds", "sounds"), &DcWalker::coop_play_sounds);
    ClassDB::bind_method(D_METHOD("coop_event_state"), &DcWalker::coop_event_state);
    ClassDB::bind_method(D_METHOD("coop_apply_event_state", "pairs"), &DcWalker::coop_apply_event_state);
    ClassDB::bind_method(D_METHOD("coop_mimic", "slot"), &DcWalker::coop_mimic);
    ClassDB::bind_method(D_METHOD("coop_debug"), &DcWalker::coop_debug);
    ClassDB::bind_method(D_METHOD("set_player_color", "index"), &DcWalker::set_player_color);
    ClassDB::bind_method(D_METHOD("is_back_floor"), &DcWalker::is_back_floor);
    ClassDB::bind_method(D_METHOD("get_coop_players"), &DcWalker::get_coop_players);
    ClassDB::bind_method(D_METHOD("has_back_floor"), &DcWalker::has_back_floor);
    ClassDB::bind_method(D_METHOD("debug_swap_floor"), &DcWalker::debug_swap_floor);
    ClassDB::bind_method(D_METHOD("debug_monster_cols"), &DcWalker::debug_monster_cols);
    ClassDB::bind_method(D_METHOD("debug_player_spheres"), &DcWalker::debug_player_spheres);
    ClassDB::bind_method(D_METHOD("debug_polys_near", "x", "z"), &DcWalker::debug_polys_near);
    ClassDB::bind_method(D_METHOD("debug_event_end"), &DcWalker::debug_event_end);
    ClassDB::bind_method(D_METHOD("debug_dran_hit", "tile"), &DcWalker::debug_dran_hit);
    ClassDB::bind_method(D_METHOD("get_dran_state"), &DcWalker::get_dran_state);
    ClassDB::bind_method(D_METHOD("debug_hit_stats"), &DcWalker::debug_hit_stats);
    ClassDB::bind_method(D_METHOD("set_hold_pad", "lx", "ly", "buttons"), &DcWalker::set_hold_pad);
    ClassDB::bind_method(D_METHOD("coop_revive"), &DcWalker::coop_revive);
    ClassDB::bind_method(D_METHOD("coop_swap_floor"), &DcWalker::debug_swap_floor);
    ClassDB::bind_method(D_METHOD("debug_kill_player"), &DcWalker::debug_kill_player);
    ClassDB::bind_method(D_METHOD("set_auto_item_pick", "on"), &DcWalker::set_auto_item_pick);
    ClassDB::bind_method(D_METHOD("modal_done", "value"), &DcWalker::modal_done);
    ClassDB::bind_method(D_METHOD("debug_set_chest_trap", "chest", "id"), &DcWalker::debug_set_chest_trap);
    ClassDB::bind_method(D_METHOD("debug_set_event_flag", "index", "value"), &DcWalker::debug_set_event_flag);
    ClassDB::bind_method(D_METHOD("override_view", "player_pos", "yaw", "camera_pos", "camera_ref"), &DcWalker::override_view);
}

void DcWalker::setup(std::shared_ptr<dccore::Dungeon> dungeon, const dccore::FloorLayout &layout, DcCharacter *player, DcDungeon *floor) {
    dungeon_ = std::move(dungeon);
    layout_ = layout;
    player_ = player;
    floor_ = floor;
    walker_ = std::make_unique<dccore::Walker>(dungeon_.get(), &layout_);
    if (const char *m = std::getenv("DC_MASK")) walker_->params.mask = std::atoi(m);   // (experiments: polygon types the player ignores)
    if (!walker_->place_at_stairs()) walker_->place_at_start();
    for (int i = 0; i < 400; ++i) if (layout_.cell[size_t(i)].part == 0x1e) visited_[size_t(i)] = 1;
    add_child(floor_);
    add_child(player_);
    camera_ = memnew(Camera3D);
    camera_->set_fov(31.3);   // 2 * atan(224 / 800): the game projects with a focal length of 800 for a 448-line screen (MGSetRenderInfo)
    camera_->set_far(6000.0);
    camera_->set_near(2.0);
    add_child(camera_);
    set_physics_process(true);
    apply_state();
}

dccore::PadRaw DcWalker::read_devices() const {
    dccore::PadRaw pad;
    if (input_blocked_) return pad;   // (a menu is open in co-op: the world goes on, the player stands still)
    Input *in = Input::get_singleton();
    auto to_raw = [](float v) { return uint8_t(std::clamp(int(std::lround(128.0 + v * 127.0)), 0, 255)); };
    const dc_pad::State joy = dc_pad::read();   // the DualShock: the PS2 pad word as the game reads it (dc_pad.h), the same buttons as in PCSX2
    float lx = joy.lx, ly = joy.ly, rx = joy.rx, ry = joy.ry;
    if (in->is_physical_key_pressed(KEY_A) || in->is_physical_key_pressed(KEY_LEFT)) lx -= 1.0f;
    if (in->is_physical_key_pressed(KEY_D) || in->is_physical_key_pressed(KEY_RIGHT)) lx += 1.0f;
    if (in->is_physical_key_pressed(KEY_W) || in->is_physical_key_pressed(KEY_UP)) ly -= 1.0f;
    if (in->is_physical_key_pressed(KEY_S) || in->is_physical_key_pressed(KEY_DOWN)) ly += 1.0f;
    if (in->is_physical_key_pressed(KEY_J)) rx -= 1.0f;
    if (in->is_physical_key_pressed(KEY_L)) rx += 1.0f;
    if (in->is_physical_key_pressed(KEY_I)) ry -= 1.0f;
    if (in->is_physical_key_pressed(KEY_K)) ry += 1.0f;
    pad.lx = to_raw(std::clamp(lx, -1.0f, 1.0f));
    pad.ly = to_raw(std::clamp(ly, -1.0f, 1.0f));
    pad.rx = to_raw(std::clamp(rx, -1.0f, 1.0f));
    pad.ry = to_raw(std::clamp(ry, -1.0f, 1.0f));
    pad.buttons |= joy.bits;
    if (in->is_physical_key_pressed(KEY_Q)) pad.buttons |= dccore::kPadCamLeft;
    if (in->is_physical_key_pressed(KEY_E)) pad.buttons |= dccore::kPadCamRight;
    if (in->is_physical_key_pressed(KEY_R)) pad.buttons |= dccore::kPadBehind;
    if (in->is_physical_key_pressed(KEY_Z)) pad.buttons |= dccore::kPadUse;   // use the quick item
    if (in->is_physical_key_pressed(KEY_BRACKETLEFT)) pad.buttons |= dccore::kPadItemLeft;
    if (in->is_physical_key_pressed(KEY_BRACKETRIGHT)) pad.buttons |= dccore::kPadItemRight;
    if (in->is_physical_key_pressed(KEY_T)) pad.buttons |= 0x10;   // triangle (the EB duel)
    if (in->is_physical_key_pressed(KEY_X)) pad.buttons |= dccore::kPadNO;   // lock on
    if (in->is_physical_key_pressed(KEY_SPACE) || in->is_physical_key_pressed(KEY_ENTER) || in->is_physical_key_pressed(KEY_F)) pad.buttons |= dccore::kPadOK;
    return pad;
}

void DcWalker::set_script(const Array &ticks) {
    script_.clear();
    for (int i = 0; i < ticks.size(); ++i) {
        Array t = ticks[i];
        dccore::PadRaw p;
        p.lx = uint8_t(int(t[0]));
        p.ly = uint8_t(int(t[1]));
        p.rx = uint8_t(int(t[2]));
        p.ry = uint8_t(int(t[3]));
        p.buttons = uint32_t(int(t[4])) | (t.size() > 5 ? uint32_t(int(t[5])) : 0u);   // (the sixth entry holds menu bits: the d-pad of the message windows)
        script_.push_back(p);
    }
}

// The mini map remembers where the player has been; entering a room marks all its cells (the function at 0x1c3b00)
void DcWalker::update_visited() {
    if (!walker_ || layout_.fixed) return;
    const float *p = walker_->player.pos;
    const int ix = int(std::floor((p[0] + 80.0f) / 160.0f)), iy = int(std::floor((p[2] + 80.0f) / 160.0f));
    int room = -1;
    for (size_t r = 0; r < layout_.rooms.size() && r < 16; ++r) {
        const dccore::RoomRect &rc = layout_.rooms[r];
        if (ix >= rc.x && ix < rc.x + rc.w && iy >= rc.y && iy < rc.y + rc.h) room = int(r);
    }
    if (room >= 0 && !room_seen_[size_t(room)]) {
        const dccore::RoomRect &rc = layout_.rooms[size_t(room)];
        if (rc.w != 0) {
            room_seen_[size_t(room)] = 1;
            for (int y = rc.y; y < rc.y + rc.h; ++y)
                for (int x = rc.x; x < rc.x + rc.w; ++x)
                    if (x >= 0 && x < 20 && y >= 0 && y < 20) visited_[size_t(y * 20 + x)] = 1;
        }
    }
    if (ix >= 0 && ix < 20 && iy >= 0 && iy < 20) visited_[size_t(iy * 20 + ix)] = 1;
}

Dictionary DcWalker::get_minimap() const {
    Dictionary d;
    d["img"] = String(dungeon_ ? dungeon_->cfg.minimap_img.c_str() : "");
    PackedInt32Array tiles, vis;
    tiles.resize(400);
    vis.resize(400);
    for (int i = 0; i < 400; ++i) {
        const dccore::MapCell &c = layout_.cell[size_t(i)];
        int tile = -1;
        if (c.part >= 0 && dungeon_ && size_t(c.part) < dungeon_->cfg.parts.size() && !dungeon_->cfg.parts[size_t(c.part)].models.empty())
            tile = c.rot + dungeon_->cfg.parts[size_t(c.part)].models[0].slot;
        tiles[i] = tile;
        vis[i] = visited_[size_t(i)];
    }
    d["tiles"] = tiles;
    d["visited"] = vis;
    if (walker_) {
        d["player"] = Vector2(walker_->player.pos[0], walker_->player.pos[2]);
        d["angle"] = walker_->camera.angle_cur;
    }
    Array mons;
    if (world_ && monsters_)
        for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i)
            if (monsters_->slots[size_t(i)].active && world_->actors[size_t(i)].hp > 0) mons.push_back(Vector2(world_->actors[size_t(i)].pos[0], world_->actors[size_t(i)].pos[2]));
    d["monsters"] = mons;
    Array items;
    if (events_)
        for (int i = 0; i < events_->ev.atra_count; ++i)
            if (events_->ev.atras[size_t(i)].active) items.push_back(Vector2(events_->ev.atras[size_t(i)].pos[0], events_->ev.atras[size_t(i)].pos[2]));
    d["items"] = items;
    return d;
}

void DcWalker::teleport(double x, double z, double y) {
    walker_->place_at(float(x), float(z), float(y));
    apply_state();
}

void DcWalker::set_start(double x, double z, double yaw, double y) {
    walker_->place_at(float(x), float(z), float(y));
    walker_->player.yaw = float(yaw);
    walker_->camera.angle = walker_->camera.angle_cur = std::atan2(std::sin(float(yaw) + 3.14159265f), std::cos(float(yaw) + 3.14159265f));
    dccore::camera_step(walker_->camera, 1);
    for (int i = 0; i < 3; ++i) { walker_->camera.pos[i] = walker_->camera.next_pos[i]; walker_->camera.ref[i] = walker_->camera.next_ref[i]; }
    apply_state();
}

void DcWalker::step_ticks(int n) {
    for (int i = 0; i < n; ++i) {
        dccore::PadRaw pad;
        if (size_t(tick_count_) < script_.size()) pad = script_[size_t(tick_count_)];
        else if (use_devices_) { pad = read_devices(); dc_pad::record(pad.lx, pad.ly, pad.rx, pad.ry, pad.buttons, 0); }   // (portkit replay: PK_RECORD_PAD)
        else if (hold_pad_on_) pad = hold_pad_;
        // The game's order inside one frame: MoveChara (the player), the monsters' Step (AI, damage), the characters' motions, then the life bar.
        const bool ok_now = (pad.buttons & dccore::kPadOK) != 0, ok_down = ok_now && !prev_ok_;
        prev_ok_ = ok_now;
        if (boss_timer_ > 0) {   // the boss fell: everything holds still while the screen fades out (MoveChara, gameTask 0x226), then the monsters are cleaned away
            ++tick_count_;
            if (--boss_timer_ == 0) {
                boss_done_ = true;
                if (monsters_) for (auto &mi : monsters_->slots) mi.active = false;
                if (boss_handler_) boss_handler_(boss_map_, boss_floor_no_);
                Dictionary d;
                d["kind"] = 30;
                d["a"] = boss_map_;
                d["b"] = boss_floor_no_;
                notice_queue_.push_back(d);
            }
            continue;
        }
        if (boss_done_) { ++tick_count_; continue; }
        if (session_) {   // (tests: where did the hero stand when an event ended?)
            const bool run_now = session_->running();
            if (dbg_ev_was_running_ && !run_now) { dbg_ev_end_pos_ = Vector3(walker_->player.pos[0], walker_->player.pos[1], walker_->player.pos[2]); dbg_ev_end_yaw_ = walker_->player.yaw; dbg_ev_end_tick_ = tick_count_; ++dbg_ev_end_count_; }
            dbg_ev_was_running_ = run_now;
        }
        if (session_ && floor_start_pending_ >= 0 && !session_->running()) {   // the story room's own start script
            const int f = floor_start_pending_;
            floor_start_pending_ = -1;
            session_->start_floor(f);
        }
        if (session_ && session_->eb_active() && eb_autoplay_ >= 0) pad = dccore::PadRaw{};   // (tests: the perfect player ignores the scripted pad)
        if (session_ && session_->eb_active()) {   // the EB duel: CGamePad::UpDate turns the left stick into the d-pad (threshold 80) and the camera / lock keys count as mistakes
            uint32_t b = pad.buttons & ~uint32_t(0xf) & ~uint32_t(0x1000 | 0x2000 | 0x4000 | 0x8000);
            const int sx = int(pad.lx) - 128, sy = int(pad.ly) - 128;
            if (sx > 80) b |= 0x2000;
            if (sx < -80) b |= 0x8000;
            if (sy > 80) b |= 0x4000;
            if (sy < -80) b |= 0x1000;
            pad.buttons = b | (pad.buttons & (0x1000 | 0x2000 | 0x4000 | 0x8000));
            if (eb_autoplay_ >= 0)   // (tests: a perfect player)
                for (const dccore::EbKey &k : session_->eb_state()->keys())
                    if (session_->eb_state()->count() - k.tick == eb_autoplay_ || (k.mask > 0 && session_->eb_state()->count() - k.tick == eb_autoplay_ + 1 && (k.mask & (k.mask - 1)) != 0))
                        pad.buttons |= k.mask;
        }
        if (session_ && session_->running()) {   // a story event runs: nothing else moves, the script has the tick
            const uint32_t down = pad.buttons & ~session_prev_buttons_;
            session_->tick(pad.buttons, down, true);
            session_prev_buttons_ = pad.buttons;
            if (ura_pending_) { ura_pending_ = false; swap_floor(); }   // (_SET_URA_DUNGEON: the script yields right after it)
            apply_object_anims();   // (the doors and wheels the script animates: _HOBJ_BT_HOBJ + _OBJ_ANIME)
            apply_event_stage();   // (the actors' clocks run with the ticks)
            walker_->prev_buttons = pad.buttons;   // (the button that closed the last message must not swing the sword)
            dccore::EvNav nav;
            if (session_->take_nav(nav)) {   // the script sent the player away (_MAP_JUMP)
                Dictionary d;
                d["kind"] = 40;
                d["a"] = nav.map;
                d["b"] = nav.event;
                notice_queue_.push_back(d);
            }
            ++tick_count_;
            continue;
        }
        if (session_) {   // the floor's own events: monsters that died, a floor that was cleared
            poll_script_markers(pad, ok_down);
            session_->tick(pad.buttons, 0, !walker_->combat.dead);
            session_prev_buttons_ = pad.buttons;
            if (session_->running()) { ++tick_count_; continue; }
        }
        if (events_ && fighting_) {   // MoveChara looks at the events before the attack buttons: pressing OK on a chest does not swing the sword
            dccore::PlayerCombat &pc = walker_->combat;
            const bool free = pc.mode == 0 && pc.invuln == 0 && pc.frozen == 0 && pc.game_task == 0;
            events_->step(walker_->player.pos, ok_down, free, world_->status.character(), world_->status, *item_db_, rng_);
            if (events_->active_event != -1 && free) pc.event_lock = 2;
            for (const dccore::Notice &n : events_->take_notices()) {
                if (n.kind == dccore::Notice::TrapChest) { start_trap_script(n.a, n.b); continue; }
                Dictionary d;
                d["kind"] = int(n.kind);
                d["a"] = n.a;
                d["b"] = n.b;
                notice_queue_.push_back(d);
                if (world_ && world_->trace) world_->log.push_back("notice " + std::to_string(int(n.kind)) + " " + std::to_string(n.a));
                if (n.kind == dccore::Notice::DoorOpened && floor_) floor_->open_door(n.a, n.b, 1);
                if (n.kind == dccore::Notice::GateOpened && floor_) floor_->open_door(n.a, n.b, 2);
                if (n.kind == dccore::Notice::ExitFloor) exit_requested_ = true;
                if (n.kind == dccore::Notice::AtraGot && atra_handler_ && world_) atra_handler_(world_->status, n.a);
                if (n.kind == dccore::Notice::Mimic && world_ && n.a >= 0 && n.a < dccore::MonsterUnit::kSlots) world_->actors[size_t(n.a)].active = 1;
                if (n.kind == dccore::Notice::Trap && world_ && world_->item_db) {
                    const dccore::TrapResult tr = dccore::trap_circle_effect(world_->status, *world_->item_db, rng_, n.a);
                    if (tr.stamina) walker_->apply_status_err(8);
                    if (tr.pump_monsters)
                        for (auto &a : world_->actors) a.bin2 = 300;
                }
            }
        }
        walker_->tick(pad);
        update_visited();
        if (world_ && coop_role_ == 2) coop_capture(2);   // (a client's sword spheres live for this tick only)
        if (session_ && fighting_) poll_hit_markers();
        if (world_) {
            const dccore::PlayerState &pl = walker_->player;   // the monsters see the player where MoveChara left him
            std::copy(pl.pos, pl.pos + 3, world_->player.pos);
            world_->player.yaw = pl.yaw;
            if (fighting_) {
                for (int k = 0; k < 3; ++k) world_->player.prev_pos[k] = walker_->prev_pos()[k];
                world_->player.facing[0] = walker_->combat.stick_dir[0]; world_->player.facing[1] = 0.0f; world_->player.facing[2] = walker_->combat.stick_dir[1]; world_->player.facing[3] = 1.0f;
                world_->player.attack_idle = walker_->combat.idle_counter;
            }
            if (fighting_) world_->hold = walker_->combat.unit_hold;
            if (coop_role_ != 2) world_->step_ai();   // (a co-op client's monsters are the host's)
            {   // (tests: how close did the player's spheres come to a body sphere that was on?)
                for (const dccore::MonsterActor &m : world_->actors) {
                    if (m.state == -1 || m.hp < 1) continue;
                    for (const dccore::BodyCol &b : m.body_cols) {
                        if (!b.active) continue;
                        if (b.win_start != 0.0f && (m.motion.time <= b.win_start || b.win_end < m.motion.time)) continue;
                        if (b.pos[0] == 0.0f && b.pos[1] == 0.0f && b.pos[2] == 0.0f) continue;   // (not placed yet)
                        const float d = dccore::vu_dist(b.pos, walker_->player.pos);
                        if (d < dbg_near_[0]) { dbg_near_[0] = d; dbg_near_[1] = b.pos[0]; dbg_near_[2] = b.pos[1]; dbg_near_[3] = b.pos[2]; dbg_near_slot_ = m.slot; dbg_near_tick_ = tick_count_; }
                    }
                }
                bool any = false;
                for (const dccore::ColEntry &e : world_->pool.e) {
                    if (!e.used || (e.target & 2) == 0 || e.owner_kind >= 200) continue;
                    any = true;
                    for (const dccore::MonsterActor &m : world_->actors) {
                        if (m.state == -1 || m.hp < 1 || m.muteki >= 1) continue;
                        for (const dccore::BodyCol &b : m.body_cols) {
                            if (!b.active) continue;
                            if (b.win_start != 0.0f && (m.motion.time <= b.win_start || b.win_end < m.motion.time)) continue;
                            const float gap = dccore::vu_dist(b.pos, e.pos) - (b.radius + e.radius);
                            if (gap < 12.0f && std::getenv("DC_GAPDBG")) std::printf("GAP tick %d slot %d gap %.2f entry (%.1f %.1f %.1f r %.1f) body %s (%.1f %.1f %.1f r %.1f) hero (%.1f %.1f %.1f) pct %d %d %d %d %d %d motion %d t %.1f\n", int(tick_count_), m.slot, gap, e.pos[0], e.pos[1], e.pos[2], e.radius, b.bone.c_str(), b.pos[0], b.pos[1], b.pos[2], b.radius, walker_->player.pos[0], walker_->player.pos[1], walker_->player.pos[2], b.attacker_pct[0], b.attacker_pct[1], b.attacker_pct[2], b.attacker_pct[3], b.attacker_pct[4], b.attacker_pct[5], int(m.motion.cur), double(m.motion.time));
                            if (gap < dbg_best_gap_) { dbg_best_gap_ = gap; dbg_best_slot_ = m.slot; dbg_best_tick_ = tick_count_; }
                        }
                    }
                }
                if (any) ++dbg_sphere_ticks_;
            }
            if (coop_role_ == 1) coop_capture(1);
            if (boss_floor_ && world_->run_script_request >= 0 && boss_timer_ == 0) {   // a boss's death script asked for event 510
                boss_timer_ = 120;
                world_->run_script_request = -1;
            }
            if (fighting_) refresh_weapon();
            if (fighting_) walker_->combat.idle_counter = world_->player.attack_idle;
            world_->step_pool();
            if (fighting_) {
                walker_->step_shake(rng_);
                world_->step_effects();
            }
            if (coop_role_ == 1) coop_capture(1);
            else if (coop_role_ == 2) coop_capture(2);
            if (fighting_) walker_->update_motion_request();
            if (coop_role_ != 2) world_->step_motions();
            if (layout_.fixed && floor_) {   // Dran's arena tiles (CDranMapField::Step): crumble when shot, the model follows
                dccore::dran_field_step(layout_);
                for (int ti : layout_.dran_events) {
                    Dictionary d;
                    d["kind"] = 33;
                    d["a"] = 0x6c9;
                    d["b"] = Vector3(0, 0, 0);
                    d["boss"] = false;
                    d["stop"] = false;
                    notice_queue_.push_back(d);
                    if (coop_role_ == 1) coop_sound_out_.push_back(d);
                    (void)ti;
                }
                layout_.dran_events.clear();
                for (int ti = 0; ti < 12; ++ti)
                    if (dran_shown_[size_t(ti)] != layout_.dran_state[size_t(ti)] || layout_.dran_state[size_t(ti)] == 1) {
                        dran_shown_[size_t(ti)] = layout_.dran_state[size_t(ti)];
                        floor_->set_dran_tile(ti, layout_.dran_state[size_t(ti)], double(layout_.dran_time[size_t(ti)]));
                    }
            }
            if (fighting_) {
                walker_->step_motion();
                walker_->step_status();
                world_->step_shot_motions();
                world_->update_lock_cursor();
                for (const dccore::CombatEvent &ev : world_->events) {   // what the weapon went through
                    if (ev.kind != dccore::CombatEvent::WeaponBroke && ev.kind != dccore::CombatEvent::WeaponRepaired && ev.kind != dccore::CombatEvent::WeaponWarn) continue;
                    if (ev.processed_notice) continue;
                    Dictionary d;
                    d["kind"] = ev.kind == dccore::CombatEvent::WeaponBroke ? 20 : (ev.kind == dccore::CombatEvent::WeaponRepaired ? 21 : 22);
                    d["a"] = ev.value;
                    d["b"] = ev.value2;
                    if (ev.remote > 0) {   // (another player's weapon: he gets the message)
                        d["remote"] = ev.remote;
                        coop_notice_out_.push_back(d);
                    } else {
                        notice_queue_.push_back(d);
                    }
                }
                for (const dccore::CombatEvent &ev : world_->events) {   // the floating numbers over the monsters
                    if (ev.kind != dccore::CombatEvent::Hit && ev.kind != dccore::CombatEvent::Blocked && ev.kind != dccore::CombatEvent::Poison) continue;
                    Dictionary h;
                    h["pos"] = Vector3(ev.pos[0], ev.pos[1] + 12.0f, ev.pos[2]);
                    h["value"] = ev.value;
                    h["type"] = ev.kind == dccore::CombatEvent::Blocked ? -1 : 0;
                    hit_values_.push_back(h);
                    if (ev.kind == dccore::CombatEvent::Hit && ev.remote > 0) ++coop_remote_hits_;
                    if (coop_role_ == 1) coop_hit_out_.push_back(h);
                }
                const int ch = world_->status.character();
                const int life_now = int(world_->status.life(ch));
                if (last_life_ >= 0 && last_life_char_ == ch && life_now < last_life_) {   // the party's damage (red)
                    Dictionary h;
                    h["pos"] = Vector3(walker_->player.pos[0], walker_->player.pos[1] + 24.0f, walker_->player.pos[2]);
                    h["value"] = last_life_ - life_now;
                    h["type"] = 2;
                    hit_values_.push_back(h);
                }
                last_life_ = life_now;
                last_life_char_ = ch;
                refresh_weapon();
                world_->events.clear();
            }
        }
        ++tick_count_;
    }
    apply_state();
    apply_monsters();
    apply_drops();
    apply_events();
    apply_shots();
    apply_throw();
}

void DcWalker::set_events(dccore::FloorEvents events, std::vector<dccore::FloorMarker> markers, std::shared_ptr<dccore::EventTables> tables, int map_no, int floor, int gate_key) {
    event_tables_ = std::move(tables);
    events_ = std::make_unique<dccore::EventWorld>();
    events_->init(std::move(events), std::move(markers), &layout_, map_no, floor, gate_key);
    ev_map_ = map_no;
    ev_floor_ = floor;
    if (!in_back_ && !back_.present) main_initial_events_ = events_->ev;
    attach_mimics(false);
    // placeholders until the chest and statue models are hooked up: boxes for chests, a sphere for atra statues, flat discs for the trigger areas
    auto flat = [](Color c, bool unshaded) {
        Ref<StandardMaterial3D> m;
        m.instantiate();
        m->set_albedo(c);
        if (unshaded) m->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
        if (c.a < 1.0f) m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        return m;
    };
    for (int i = 0; i < events_->ev.chest_count; ++i) make_chest_node(events_->ev.chests[size_t(i)]);
    for (int i = 0; i < events_->ev.atra_count; ++i) {
        const dccore::AtraBall &a = events_->ev.atras[size_t(i)];
        MeshInstance3D *m = memnew(MeshInstance3D);
        Ref<SphereMesh> sph;
        sph.instantiate();
        sph->set_radius(5.0);
        sph->set_height(10.0);
        m->set_mesh(sph);
        m->set_material_override(flat(Color(0.3f, 0.9f, 1.0f), true));
        m->set_position(Vector3(a.pos[0], a.pos[1] + 8.0f, a.pos[2]));
        add_child(m);
        atra_nodes_.push_back(m);
    }
    for (const dccore::TrapCircle &t : events_->ev.traps) {
        MeshInstance3D *m = memnew(MeshInstance3D);
        Ref<CylinderMesh> cyl;
        cyl.instantiate();
        cyl->set_top_radius(9.0);
        cyl->set_bottom_radius(9.0);
        cyl->set_height(0.2);
        m->set_mesh(cyl);
        m->set_material_override(flat(Color(0.7f, 0.2f, 0.9f, 0.45f), true));
        m->set_position(Vector3(t.pos[0], t.pos[1] + 0.3f, t.pos[2]));
        m->set_visible(t.state == 1);
        add_child(m);
        trap_nodes_.push_back(m);
    }
    for (const dccore::FloorMarker &mk : events_->markers) {
        MeshInstance3D *m = memnew(MeshInstance3D);
        Ref<CylinderMesh> cyl;
        cyl.instantiate();
        cyl->set_top_radius(mk.marker.radius);
        cyl->set_bottom_radius(mk.marker.radius);
        cyl->set_height(0.3);
        m->set_mesh(cyl);
        Color col(0.6f, 0.6f, 0.6f, 0.25f);
        if (mk.marker.event_id == 160) col = Color(0.2f, 1.0f, 0.3f, 0.35f);
        else if (mk.marker.event_id == 161) col = Color(1.0f, 0.2f, 0.2f, 0.35f);
        else if (mk.marker.event_id == 250 || mk.marker.event_id == 251) col = Color(1.0f, 0.6f, 0.1f, 0.35f);
        else m->set_visible(false);
        m->set_material_override(flat(col, true));
        m->set_position(Vector3(mk.pos[0], mk.pos[1] + 0.2f, mk.pos[2]));
        add_child(m);
        marker_nodes_.push_back(m);
    }
}

// The monsters that are mimics wait inside a chest (kind 3 small, 4 big). `reset`: the floor was in play before (RsetMimicEvent drops the old ones first).
void DcWalker::attach_mimics(bool reset) {
    if (!events_ || !world_ || !monsters_) return;
    if (reset)
        for (dccore::FloorEvent &e : events_->ev.events)
            if (e.type == 8) {
                if (e.index >= 0 && size_t(e.index) < events_->ev.chests.size()) events_->ev.chests[size_t(e.index)].active = false;
                e.type = -1;
            }
    for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i) {
        const dccore::MonsterActor &a = world_->actors[size_t(i)];
        if (a.state == -1 || a.monster_code != 8 || a.active != 0) continue;
        const dccore::MonsterInstance &mi = monsters_->slots[size_t(i)];
        if (mi.base < 0 || size_t(mi.base) >= monsters_->bases.size()) continue;
        const int kind = monsters_->bases[size_t(mi.base)].entry.kind();
        if (kind != 3 && kind != 4) continue;
        const float p[4] = {a.pos[0], a.pos[1], a.pos[2], 1.0f};
        events_->ev.set_mimic(p, kind == 3, i);
    }
}

// The chest nodes follow the chest list (a mimic put into a slot that held an older one moves its box).
void DcWalker::sync_chest_nodes() {
    if (!events_) return;
    for (int i = 0; i < events_->ev.chest_count; ++i) {
        const dccore::Chest &c = events_->ev.chests[size_t(i)];
        if (size_t(i) >= chest_nodes_.size()) { make_chest_node(c); continue; }
        chest_nodes_[size_t(i)]->set_position(Vector3(c.pos[0], c.pos[1] + 4.5f, c.pos[2]));
        chest_nodes_[size_t(i)]->set_visible(c.active);
    }
}

void DcWalker::set_back_floor(const dccore::FloorLayout &layout, dccore::FloorEvents events, std::vector<dccore::FloorMarker> markers, std::function<DcDungeon *(const dccore::FloorLayout &)> make_node,
        std::function<void(dccore::MonsterUnit &, bool)> load_monsters) {
    back_.present = true;
    back_.events = std::move(events);
    back_.markers = std::move(markers);
    back_.make_node = std::move(make_node);
    back_.load_monsters = std::move(load_monsters);
    shelf_ = FloorSlot{};
    shelf_.layout = layout;
}

// gameTask 0xa0: main <-> back. The monsters of the one unit are wiped and the other floor's row is loaded (8 monsters on the back floor, 15 fresh ones back on the main floor);
// chests, doors and the explored cells stay with their floor.
void DcWalker::swap_floor() {
    if (!back_.present || !world_ || !monsters_ || !game_tables_) return;
    const bool to_back = !in_back_;
    std::swap(layout_, shelf_.layout);   // (the walker, the monsters and the events point at layout_ itself)
    std::swap(events_, shelf_.events);
    std::swap(floor_, shelf_.node);
    chest_nodes_.swap(shelf_.chests);
    atra_nodes_.swap(shelf_.atras);
    marker_nodes_.swap(shelf_.markers);
    trap_nodes_.swap(shelf_.traps);
    std::swap(visited_, shelf_.visited);
    std::swap(room_seen_, shelf_.room_seen);
    script_inside_.swap(shelf_.inside);
    hint_char_.swap(shelf_.hint);
    in_back_ = to_back;
    if (shelf_.node) shelf_.node->set_visible(false);
    for (auto *list : {&shelf_.chests, &shelf_.atras, &shelf_.markers, &shelf_.traps})
        for (Node3D *n : *list) if (n) n->set_visible(false);
    // the monsters
    world_->clear_monsters();
    if (back_.load_monsters) back_.load_monsters(*monsters_, to_back);
    world_->double_weapon_exp = to_back;
    // (co-op: every peer arranges the same monsters, whatever its generator has done so far, from the floor's own seed and the chests as built)
    const bool coop = coop_role_ != 0;
    const dccore::FloorEvents *evp = coop ? (to_back ? &back_.events : &main_initial_events_) : (events_ ? &events_->ev : &back_.events);
    const uint32_t saved_rng = rng_.state;
    if (coop) rng_.seed(layout_.seed * 2654435761u + (to_back ? 0xB4C4u : 0xA1A1u));
    monsters_->arrange(layout_, *game_tables_, ev_map_, rng_, dccore::coop_spawn_count(to_back ? 8 : 15, coop_players_), -1, [this](int slot) {
        world_->spawn(slot);
        for (int k = 0; k < 3; ++k) monsters_->slots[size_t(slot)].pos[k] = world_->actors[size_t(slot)].pos[k];
    }, evp);
    world_->set_key(ev_map_, ev_floor_, layout_.map1_variant);   // (returns at once on the back floor: no gate key there)
    if (coop) rng_.seed(saved_rng);
    build_monster_nodes();
    if (!events_) {   // the first visit: the floor's node and its events are made now
        if (back_.make_node) floor_ = back_.make_node(layout_);
        if (floor_) add_child(floor_);
        set_events(back_.events, back_.markers, event_tables_, ev_map_, ev_floor_, 0);
    } else {
        if (floor_) floor_->set_visible(true);
        for (auto *list : {&chest_nodes_, &atra_nodes_, &marker_nodes_, &trap_nodes_})
            for (Node3D *n : *list) if (n) n->set_visible(true);
        attach_mimics(true);
        sync_chest_nodes();
    }
    // the hero stands at the entrance of the floor he came to (the scene then places him exactly: _SET_WORLD_COORD / _SET_NPC_POS)
    for (const dccore::FloorMarker &fm : events_->markers)
        if (fm.marker.event_id == 200) {
            walker_->place_at(fm.pos[0], fm.pos[2], fm.pos[1] + 4.0f);
            break;
        }
    std::copy(walker_->player.pos, walker_->player.pos + 3, world_->player.pos);
    Dictionary d;
    d["kind"] = 41;
    d["a"] = in_back_ ? 1 : 0;
    notice_queue_.push_back(d);
}

void DcWalker::coop_revive() {
    if (!walker_ || !world_) return;
    const int ch = world_->status.character();
    const int hp = std::max(1, int(world_->status.max_life(ch)) / 2);
    world_->status.set_life(ch, int16_t(hp));
    world_->status.set<int16_t>(0x8a5c + size_t(ch) * 2, int16_t(hp));
    world_->status.set<int16_t>(0x8a68 + size_t(ch) * 2, 0);
    world_->status.set<int32_t>(0x8b0c, 0);   // (the death animation raised it)
    if (!walker_->place_at_stairs()) walker_->place_at_start();
    dccore::PlayerCombat &pc = walker_->combat;
    pc.dead = false;
    pc.game_over = false;
    pc.game_task = 0;
    pc.mode = 0;
    pc.frozen = 0;
    pc.busy = 0;
    pc.wish = 0;
    pc.requested = -1;
    pc.no_interpolate = true;
    pc.invuln = 240;
    walker_->drive_step_hold = false;
    last_life_ = -1;
}

void DcWalker::start_trap_script(int chest, int trap) {
    if (!events_ || chest < 0 || size_t(chest) >= events_->ev.chests.size()) return;
    dccore::Chest &c = events_->ev.chests[size_t(chest)];
    const int fid = trap == 5 ? 16 : 15;
    if (!session_ || !session_->has_function(fid)) { c.quality = 0; return; }   // (no script to ask: the chest opens)
    ibox_ = chest;
    const float pos[3] = {c.pos[0], c.pos[1], c.pos[2] + (c.small ? 8.0f : 10.0f)};   // (the event area's centre)
    session_->start(fid, 1, pos, false);
}

void DcWalker::apply_events() {
    if (!events_) return;
    for (int i = 0; i < events_->ev.chest_count; ++i)
        if (size_t(i) < chest_nodes_.size()) {
            const dccore::Chest &c = events_->ev.chests[size_t(i)];
            chest_nodes_[size_t(i)]->set_scale(c.state == 1 ? Vector3(1, 0.5f, 1) : Vector3(1, 1, 1));
            if (!c.active) chest_nodes_[size_t(i)]->set_visible(false);   // (a mimic's chest is gone once it opened)
        }
    for (int i = 0; i < events_->ev.atra_count; ++i)
        if (size_t(i) < atra_nodes_.size()) atra_nodes_[size_t(i)]->set_visible(events_->ev.atras[size_t(i)].active);
    for (size_t i = 0; i < trap_nodes_.size() && i < events_->ev.traps.size(); ++i) trap_nodes_[i]->set_visible(events_->ev.traps[i].state == 1);
    for (size_t i = 0; i < events_->markers.size() && i < marker_nodes_.size(); ++i) {
        const int id = events_->markers[i].marker.event_id;
        // (the stairs, the gate and the doors show a disc while they work; the other trigger areas are invisible, as in the game)
        marker_nodes_[i]->set_visible((id == 160 || id == 161 || id == 250 || id == 251) && events_->marker_state[i].on);
    }
}

Array DcWalker::take_notices() {
    Array out = notice_queue_;
    notice_queue_ = Array();
    return out;
}

void DcWalker::_physics_process(double delta) {
    // The engine runs physics at 60 Hz (project setting), one game tick per call; catch up if frames were slow.
    accumulator_ += delta * 60.0;
    int n = int(accumulator_);
    accumulator_ -= n;
    if (n > 4) n = 4;
    if (n > 0) step_ticks(n);
}

void DcWalker::apply_state() {
    const dccore::PlayerState &p = walker_->player;
    const bool staged = session_ && session_->running();
    const dccore::CameraState &c = staged ? session_->rt().camera.cs : walker_->camera;
    if (player_ && !staged) {
        player_->set_position(Vector3(p.pos[0], p.pos[1], p.pos[2]));
        player_->set_rotation(Vector3(0, p.yaw, 0));
        if (!stage_was_running_) player_->set_visible(!walker_->view_mode);   // (the first person view does not show the character)
        if (fighting_) {   // the simulation owns the motion state (animation, blending, hurt/death sequences)
            player_->set_playing(false);
            if (player_->get_motion() != walker_->motion.cur) player_->play(walker_->motion.cur, false);
            player_->set_playing(false);
            player_->set_time(walker_->motion.time);
        } else if (p.motion != last_motion_) {
            player_->play(p.motion, true);
            last_motion_ = p.motion;
        }
        if (!fighting_) {   // run animation speed follows the stick (keyCtrl), idle plays slowly
            const Dictionary info = player_->get_motion_info(p.motion);
            const double table = info.has("speed") ? double(info["speed"]) : 1.0;
            double wanted = table;
            if (p.motion == 0) wanted = 0.05;
            else if (p.motion == 1) wanted = p.motion_speed;
            player_->set_speed_scale(table > 0.0 ? wanted / table : 1.0);
        }
    }
    if (weapon_node_ && player_ && fighting_) {   // the sword hangs on the hand frame of the pose just shown
        dccore::Mat4 identity{};
        identity[0] = identity[5] = identity[10] = identity[15] = 1.0;
        weapon_node_->set_transform(player_->attach_transform(walker_->weapon_frame, identity));
    }
    if (camera_) {
        const Vector3 cam_pos(c.pos[0], c.pos[1], c.pos[2]), ref(c.ref[0], c.ref[1], c.ref[2]);
        if (cam_pos.distance_squared_to(ref) > 1e-6) {
            Transform3D t;
            t.origin = cam_pos;
            camera_->set_transform(t.looking_at(ref, Vector3(0, 1, 0)));   // (look_at needs the node inside the tree; the first state is applied before that)
        } else {
            camera_->set_position(cam_pos);
        }
    }
}

Vector3 DcWalker::get_player_position() const {
    const auto &p = walker_->player.pos;
    return Vector3(p[0], p[1], p[2]);
}

Dictionary DcWalker::get_camera_state() const {
    const dccore::CameraState &c = walker_->camera;
    Dictionary d;
    d["pos"] = Vector3(c.pos[0], c.pos[1], c.pos[2]);
    d["ref"] = Vector3(c.ref[0], c.ref[1], c.ref[2]);
    d["dist"] = c.dist;
    d["height"] = c.height;
    d["angle"] = c.angle;
    return d;
}

void DcWalker::override_view(const Vector3 &player_pos, double yaw, const Vector3 &camera_pos, const Vector3 &camera_ref) {
    set_physics_process(false);
    if (player_) {
        player_->set_position(player_pos);
        player_->set_rotation(Vector3(0, real_t(yaw), 0));
    }
    if (camera_) {
        camera_->set_position(camera_pos);
        camera_->look_at(camera_ref, Vector3(0, 1, 0));
    }
}

void DcWalker::set_monsters(std::shared_ptr<dccore::MonsterUnit> unit, const dccore::GameTables &tables, int map_no, uint32_t rand_state, dccore::CollisionPiece monster_shape, const dccore::FloorEvents *events, int floor, int map1_variant) {
    monsters_ = std::move(unit);
    game_tables_ = &tables;
    rng_.seed(rand_state);
    world_ = std::make_unique<dccore::MonsterWorld>(dungeon_.get(), &layout_, monsters_.get(), &rng_);
    world_->set_monster_shape(std::move(monster_shape));
    world_->floor_map = map_no;
    world_->set_coop_players(coop_players_);
    const dccore::PlayerState &pl = walker_->player;
    std::copy(pl.pos, pl.pos + 3, world_->player.pos);
    if (boss_floor_) {   // the boss and its helpers: view slots in the order of the layout row (BtLoadMonstor + SetupViewMonstor)
        boss_map_ = map_no;
        boss_floor_no_ = floor;
        world_->boss_free_flight = true;
        for (size_t b = 0; b < monsters_->bases.size() && b < size_t(dccore::MonsterUnit::kSlots); ++b) {
            dccore::MonsterInstance &mi = monsters_->slots[b];
            mi.active = true;
            mi.base = int(b);
            mi.pos[0] = mi.pos[1] = mi.pos[2] = 0.0f;
            world_->spawn(int(b));
            for (int k = 0; k < 3; ++k) mi.pos[k] = world_->actors[b].pos[k];
        }
    } else if (!layout_.fixed)   // (the monsters of a fixed map come with its events)
        monsters_->arrange(layout_, tables, map_no, rng_, dccore::coop_spawn_count(15, coop_players_), -1, [this](int slot) {
            world_->spawn(slot);
            for (int k = 0; k < 3; ++k) monsters_->slots[size_t(slot)].pos[k] = world_->actors[size_t(slot)].pos[k];
        }, events);
    world_->status = initial_status_ ? *initial_status_ : dccore::UserStatus::new_game();
    if (!layout_.fixed) world_->set_key(map_no, floor, map1_variant);
    enable_combat();
    build_monster_nodes();
}

// One model per active monster slot (the old ones go).
void DcWalker::build_monster_nodes() {
    for (DcCharacter *old : monster_nodes_)
        if (old) old->queue_free();
    monster_nodes_.assign(size_t(dccore::MonsterUnit::kSlots), nullptr);
    for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i) {
        const dccore::MonsterInstance &m = monsters_->slots[size_t(i)];
        if (!m.active) continue;
        const dccore::MonsterBase &b = monsters_->bases[size_t(m.base)];
        DcCharacter *node = memnew(DcCharacter);
        Dictionary info;
        node->setup(b.model, info);
        node->set_playing(false);   // the simulation sets the motion and its time
        node->set_position(Vector3(m.pos[0], m.pos[1], m.pos[2]));
        node->set_name(String(("monster_" + std::to_string(i) + "_" + b.entry.stem()).c_str()));
        add_child(node);
        monster_nodes_[size_t(i)] = node;
    }
    apply_monsters();
}

void DcWalker::apply_drops() {
    if (!world_) return;
    if (drop_nodes_.empty()) {
        drop_nodes_.assign(size_t(dccore::Drops::kSize), nullptr);
        for (int i = 0; i < dccore::Drops::kSize; ++i) {
            MeshInstance3D *m = memnew(MeshInstance3D);
            Ref<CylinderMesh> cyl;
            cyl.instantiate();
            cyl->set_top_radius(3.0);
            cyl->set_bottom_radius(3.0);
            cyl->set_height(0.8);
            m->set_mesh(cyl);
            m->set_visible(false);
            add_child(m);
            drop_nodes_[size_t(i)] = m;
        }
    }
    for (int i = 0; i < dccore::Drops::kSize; ++i) {
        const dccore::Drop &d = world_->drops.d[size_t(i)];
        Node3D *n = drop_nodes_[size_t(i)];
        n->set_visible(d.used && d.state != 2);
        if (!d.used) continue;
        const float bob = d.state == 0 ? std::sin(d.timer) * 8.0f : 0.0f;
        n->set_position(Vector3(d.pos[0], d.pos[1] + bob, d.pos[2]));
        n->set_rotation(Vector3(1.5707963267948966, world_->drops.spin * 2.0f, 0));
        MeshInstance3D *m = Object::cast_to<MeshInstance3D>(n);
        Ref<StandardMaterial3D> mat;
        mat.instantiate();
        mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
        mat->set_albedo(d.gold >= 0 ? Color(1.0, 0.85, 0.1) : Color(0.3, 0.7, 1.0));
        m->set_material_override(mat);
    }
}

void DcWalker::apply_monsters() {
    if (!world_) return;
    for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i) {
        DcCharacter *node = monster_nodes_.empty() ? nullptr : monster_nodes_[size_t(i)];
        if (!node) continue;
        const dccore::MonsterActor &a = world_->actors[size_t(i)];
        node->set_visible(a.state == 2);   // the game draws only awake monsters
        if (a.state != 2) continue;
        node->set_position(Vector3(a.pos[0], a.pos[1], a.pos[2]));
        node->set_rotation(Vector3(0, a.rot[1], 0));
        if (node->get_motion() != a.motion.cur) node->play(a.motion.cur, false);
        node->set_time(a.motion.time);
    }
}

Array DcWalker::debug_polys_near(double x, double z) const {
    Array out;
    if (!dungeon_) return out;
    for (const dccore::CCPoly &q : dccore::floor_polys_near(*dungeon_, layout_, float(x), float(z), 0)) {
        Dictionary d;
        d["v0"] = Vector3(q.v[0][0], q.v[0][1], q.v[0][2]);
        d["v1"] = Vector3(q.v[1][0], q.v[1][1], q.v[1][2]);
        d["v2"] = Vector3(q.v[2][0], q.v[2][1], q.v[2][2]);
        d["n"] = Vector3(q.n[0], q.n[1], q.n[2]);
        d["attr"] = int(q.attr[0]);
        out.push_back(d);
    }
    return out;
}

Array DcWalker::debug_player_spheres() const {
    Array out;
    if (!world_) return out;
    for (const dccore::ColEntry &e : world_->pool.e) {
        if (!e.used || (e.target & 2) == 0) continue;
        Dictionary d;
        d["pos"] = Vector3(e.pos[0], e.pos[1], e.pos[2]);
        d["r"] = e.radius;
        d["owner"] = e.owner_kind;
        out.push_back(d);
    }
    return out;
}

Array DcWalker::debug_monster_cols() const {
    Array out;
    if (!world_ || !monsters_) return out;
    for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i) {
        const dccore::MonsterActor &a = world_->actors[size_t(i)];
        if (a.state == -1 || !monsters_->slots[size_t(i)].active) continue;
        Dictionary d;
        d["slot"] = i;
        d["name"] = String(monsters_->bases[size_t(monsters_->slots[size_t(i)].base)].entry.stem().c_str());
        d["hp"] = a.hp;
        d["muteki"] = a.muteki;
        d["col_off"] = a.col_off;
        d["motion"] = a.motion.cur;
        d["time"] = a.motion.time;
        d["pos"] = Vector3(a.pos[0], a.pos[1], a.pos[2]);
        Array cols;
        for (const dccore::BodyCol &b : a.body_cols) {
            if (!b.active) continue;
            Dictionary c;
            c["bone"] = String(b.bone.c_str());
            c["r"] = b.radius;
            c["pos"] = Vector3(b.pos[0], b.pos[1], b.pos[2]);
            c["win"] = Vector2(b.win_start, b.win_end);
            { Array pc; for (int v : b.attacker_pct) pc.push_back(v); c["pct"] = pc; }
            cols.push_back(c);
        }
        d["cols"] = cols;
        out.push_back(d);
    }
    return out;
}

Array DcWalker::get_monster_info() const {
    Array out;
    if (!monsters_) return out;
    for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i) {
        const dccore::MonsterInstance &m = monsters_->slots[size_t(i)];
        if (!m.active) continue;
        Dictionary d;
        d["slot"] = i;
        d["name"] = String(monsters_->bases[size_t(m.base)].entry.stem().c_str());
        d["pos"] = world_ ? Vector3(world_->actors[size_t(i)].pos[0], world_->actors[size_t(i)].pos[1], world_->actors[size_t(i)].pos[2]) : Vector3(m.pos[0], m.pos[1], m.pos[2]);
        if (world_) {
            d["state"] = world_->actors[size_t(i)].state;
            d["hp"] = world_->actors[size_t(i)].hp;
            d["item"] = int(world_->actors[size_t(i)].dropped_item);
            d["motion"] = world_->actors[size_t(i)].motion.cur;
            d["active"] = int(world_->actors[size_t(i)].active);
            d["code"] = world_->actors[size_t(i)].monster_code;
            d["lockon_sw"] = world_->actors[size_t(i)].lockon_sw;
            d["lockon_dist"] = world_->actors[size_t(i)].lockon_dist;
            d["lock_bone"] = String(world_->actors[size_t(i)].lock_bone.c_str());
            d["lock_pos"] = Vector3(world_->actors[size_t(i)].lock_pos[0], world_->actors[size_t(i)].lock_pos[1], world_->actors[size_t(i)].lock_pos[2]);
            d["kind"] = monsters_->bases[size_t(m.base)].entry.kind();
        }
        out.push_back(d);
    }
    return out;
}

void DcWalker::enable_combat() {
    if (!world_ || !player_ || fighting_) return;
    world_->status = initial_status_ ? *initial_status_ : dccore::UserStatus::new_game();
    world_->item_db = item_db_.get();
    walker_->status = &world_->status;
    walker_->pool = &world_->pool;
    walker_->rng = &rng_;
    walker_->motion_table.clear();
    if (kit_) walker_->motion_table = kit_->motions;
    else
        for (int i = 0; i < player_->get_motion_count(); ++i) {
            const Dictionary m = player_->get_motion_info(i);
            walker_->motion_table.push_back({int(m["start"]), int(m["end"]), float(double(m["speed"]))});
        }
    walker_->item_db = item_db_.get();
    walker_->monster_collide = [this](const float pos[3], float vel[3], int ch) { world_->move_check_player(pos, vel, ch, false); };
    walker_->nearest_monster = [this]() {
        float best = 10000.0f;
        for (const dccore::MonsterActor &a : world_->actors)
            if (a.slot >= 0 && a.state != -1 && a.active != 0) best = std::min(best, dccore::vu_dist(walker_->player.pos, a.pos));
        return best;
    };
    walker_->on_monster_hurts_player = [this](int slot, int damage, int stolen) {
        dccore::MonsterActor &a = world_->actors[size_t(slot)];
        if (damage >= 0) a.hit_vol = damage;
        if (stolen > 0) a.stolen_gold += stolen;
    };
    if (kit_) {
        walker_->setup_weapon(kit_->model.get(), kit_->tip, kit_->tip2, kit_->weapon_model.get());
        if (kit_->has_main_fx) world_->load_player_fx(kit_->main_fx, kit_->main_fx_model, kit_->main_fx_row);
    } else {
        const float zero[3] = {0, 0, 0};
        walker_->setup_weapon(player_->get_character().get(), zero);
    }
    walker_->main_shot = [this](const dccore::Walker::MainShot &m) -> int { return world_->fire_player_shot(m.pos, m.target, m.owner_kind, m.sub_id, m.damage, m.wait, m.force_phase, m.set_attribute, m.set_damage, m.loop, m.random_rate, m.no_sound, m.attach); };
    walker_->off_main_effect = [this](int k) { world_->off_player_effect(k); };
    walker_->lock = &world_->lock;
    walker_->monster_pos = [this](int m, float out[3]) { if (m < 0 || m >= dccore::MonsterUnit::kSlots) return false; for (int k = 0; k < 3; ++k) out[k] = world_->actors[size_t(m)].pos[k]; return true; };
    walker_->shift_target = [this](int s) { return world_->set_near_target(s, false); };
    if (kit_) walker_->osmond_type = kit_->osmond_type;
    walker_->fire_mg = [this](const float p[3], const float v[3], int dmg) { world_->fire_mg(p, v, dmg); };
    walker_->firebar_init = [this](const float p[3], const float dir[3]) { world_->firebar_init(p, dir); };
    walker_->firebar_set = [this](const float p[3], const float dir[3], int dmg, int el) { world_->firebar_set(p, dir, dmg, el); };
    walker_->firebar_reset = [this]() { world_->firebar_reset(); };
    walker_->end_main_shot = [this]() { world_->end_player_shot(); };
    walker_->fire_bullet = [this](const dccore::Walker::Bullet &b) { world_->fire_sling(b.pos, b.vel, b.damage); };
    walker_->sword_dmg = [this](float p) { world_->sword_dmg_check(p, 0); };
    walker_->throw_item = [this](int id, const float p[3], const float v[3]) { world_->throw_item(id, p, v); };
    walker_->play_se = [this](int se) {
        Dictionary d;
        d["kind"] = 31;   // a sound effect of the player's actions
        d["a"] = se;
        d["b"] = 0;
        notice_queue_.push_back(d);
    };
    walker_->player.info.grounded = 1;
    shown_weapon_ = int(world_->status.weapon(world_->status.character()).id());
    fighting_ = true;
}

void DcWalker::refresh_weapon() {
    if (!fighting_ || !world_ || !weapon_changer_) return;
    const int ch = world_->status.character();
    const int id = int(world_->status.weapon(ch).id());
    if (id == shown_weapon_) return;
    shown_weapon_ = id;
    std::shared_ptr<dccore::PlayerKit> kit = weapon_changer_(ch, id, int(world_->status.weapon(ch).element_choice()));
    if (!kit) return;
    kit->model = kit_ ? kit_->model : kit->model;
    DcCharacter *wnode = memnew(DcCharacter);
    Dictionary winfo;
    wnode->setup(kit->weapon_model, winfo);
    wnode->set_playing(false);
    if (weapon_node_) {
        player_->remove_child(weapon_node_);
        weapon_node_->queue_free();
    }
    weapon_node_ = nullptr;
    set_weapon_node(wnode);
    kit_ = kit;
    walker_->setup_weapon(kit->model.get(), kit->tip, kit->tip2, kit->weapon_model.get());
    if (world_->player_fx >= 0) {
        world_->shots.slot[size_t(world_->player_fx)] = dccore::ShotEffect();
        world_->player_fx = -1;
    }
    if (kit->has_main_fx) world_->load_player_fx(kit->main_fx, kit->main_fx_model, kit->main_fx_row);
    walker_->osmond_type = kit->osmond_type;
}

bool DcWalker::swap_character(std::shared_ptr<dccore::PlayerKit> kit, DcCharacter *new_player, DcCharacter *new_weapon) {
    if (!fighting_ || !world_ || !kit || !new_player) return false;
    DcCharacter *old = player_;
    if (old) {
        remove_child(old);
        old->queue_free();
    }
    player_ = new_player;
    add_child(player_);
    if (color_index_ >= 0) player_->set_player_color(color_index_);
    weapon_node_ = nullptr;
    if (new_weapon) set_weapon_node(new_weapon);
    kit_ = kit;
    world_->status.set<int8_t>(4, int8_t(kit->character));
    walker_->motion_table = kit->motions;
    walker_->setup_weapon(kit->model.get(), kit->tip, kit->tip2, kit->weapon_model.get());
    dccore::PlayerCombat &c = walker_->combat;
    c.mode = 0;
    c.action = 0;
    c.wish = 0;
    c.requested = -1;
    c.gauge = 100.0f;
    c.overheat = 0;
    c.charge = 1.0f;
    c.in_swing = 0;
    c.chain = 0;
    c.lunge = 0.0f;
    c.busy = 0;
    c.held = 0;
    c.can_act = 1;
    c.effect_id = -1;
    walker_->motion = dccore::MotionState{};
    walker_->request = dccore::MotionRequest{};
    world_->lock_off();
    if (world_->player_fx >= 0) {
        world_->shots.slot[size_t(world_->player_fx)] = dccore::ShotEffect();
        world_->player_fx = -1;
    }
    for (auto &b : world_->bullets) b.active = false;
    if (kit->has_main_fx) world_->load_player_fx(kit->main_fx, kit->main_fx_model, kit->main_fx_row);
    walker_->osmond_type = kit->osmond_type;
    apply_state();
    return true;
}

Dictionary DcWalker::get_player_status() const {
    Dictionary d;
    if (!fighting_) return d;
    const int ch = world_->status.character();
    d["life"] = int(world_->status.life(ch));
    d["max_life"] = int(world_->status.max_life(ch));
    d["mode"] = walker_->combat.mode;
    d["invuln"] = walker_->combat.invuln;
    d["dead"] = walker_->combat.game_task != 0;
    d["game_over"] = walker_->combat.game_over;
    d["gold"] = int(world_->status.get<uint16_t>(0x4346));
    d["gauge"] = walker_->combat.gauge;
    const dccore::WeaponView w = world_->status.weapon(ch);
    d["water"] = world_->status.get<float>(0x4304 + size_t(ch) * 4);
    d["max_water"] = world_->status.get<float>(0x42ec + size_t(ch) * 4);
    Array quick;
    for (int k = 0; k < 3; ++k) {
        Dictionary q;
        q["id"] = int(world_->status.get<int16_t>(0x4362 + size_t(k) * 2));
        q["count"] = int(world_->status.get<int16_t>(0x4368 + size_t(k) * 2));
        quick.push_back(q);
    }
    d["quick"] = quick;
    d["quick_sel"] = walker_->active_sel;
    d["motion"] = walker_->motion.cur;
    d["motion_time"] = walker_->motion.time;
    d["wish"] = walker_->combat.wish;
    d["lock"] = walker_->lock ? walker_->lock->flag : 0;
    d["weapon_id"] = int(w.id());
    d["weapon_exp"] = int(w.exp());
    d["weapon_durability"] = w.durability();
    d["weapon_max_durability"] = int(w.max_durability());
    d["character"] = ch;
    d["status_err"] = int(world_->status.get<uint32_t>(0x42c8 + size_t(ch) * 4));
    d["weapon_max_exp"] = item_db_ ? item_db_->weapon_max_exp(w.p) : 0;
    d["have_map"] = events_ && events_->have_map;
    d["have_crystal"] = events_ && events_->have_crystal;
    d["map_mode"] = int(world_->status.get<int32_t>(0x433c));
    const int tgt = world_->lock.target;
    if (tgt >= 0 && tgt < dccore::MonsterUnit::kSlots && world_->actors[size_t(tgt)].hp > 0) {   // the monster in the cursor (its life bar and the corner marks)
        const auto &act = world_->actors[size_t(tgt)];
        Dictionary t;
        t["slot"] = tgt;
        t["hp"] = act.hp;
        t["hp_max"] = act.hp_max;
        float aim[3];
        world_->lock_aim_pos(act, aim);
        t["pos"] = Vector3(aim[0], aim[1], aim[2]);   // (the cursor and the aim point)
        t["locked"] = world_->lock.flag != 0;
        d["target"] = t;
    }
    return d;
}

Dictionary DcWalker::get_event_info() const {
    Dictionary out;
    if (!events_) return out;
    Array chests, atras, markers;
    for (int i = 0; i < events_->ev.chest_count; ++i) {
        const dccore::Chest &c = events_->ev.chests[size_t(i)];
        Dictionary d;
        d["pos"] = Vector3(c.pos[0], c.pos[1], c.pos[2]);
        d["item"] = c.item;
        d["small"] = c.small != 0;
        d["opened"] = c.state == 1;
        d["trap"] = c.quality;
        chests.push_back(d);
    }
    for (int i = 0; i < events_->ev.atra_count; ++i) {
        const dccore::AtraBall &a = events_->ev.atras[size_t(i)];
        Dictionary d;
        d["pos"] = Vector3(a.pos[0], a.pos[1], a.pos[2]);
        d["id"] = a.id;
        d["active"] = a.active;
        atras.push_back(d);
    }
    for (size_t i = 0; i < events_->markers.size(); ++i) {
        const dccore::FloorMarker &m = events_->markers[i];
        Dictionary d;
        d["name"] = String(m.marker.name.c_str());
        d["event"] = m.marker.event_id;
        d["pos"] = Vector3(m.pos[0], m.pos[1], m.pos[2]);
        d["radius"] = m.marker.radius;
        d["on"] = events_->marker_state[i].on;
        markers.push_back(d);
    }
    out["active_event"] = events_->active_event;
    out["chests"] = chests;
    out["atras"] = atras;
    out["markers"] = markers;
    return out;
}

Array DcWalker::get_bag() const {
    Array out;
    if (!world_) return out;
    const dccore::UserStatus &st = world_->status;
    for (int i = 0; i < dccore::kBagSlots; ++i) {
        const int id = st.get<int16_t>(0x436e + size_t(i) * 2);
        if (id < 0x84 || id > 0x100) continue;
        Dictionary d;
        d["slot"] = i;
        d["id"] = id;
        d["value"] = int(st.get<int16_t>(0x443c + size_t(i) * 2));
        out.push_back(d);
    }
    return out;
}

Dictionary DcWalker::use_bag_item(int slot) {
    Dictionary d;
    d["used"] = false;
    if (!world_ || !item_db_ || slot < 0 || slot >= dccore::kBagSlots) return d;
    const int id = world_->status.get<int16_t>(0x436e + size_t(slot) * 2);
    if (id < 0x84 || id > 0x100) return d;
    const dccore::UseEffect e = dccore::use_item_effect(world_->status, *item_db_, id);
    if (!e.used) return d;
    if (e.poisoned) walker_->apply_status_err(8);
    dccore::consume_bag_slot(world_->status, slot);
    d["used"] = true;
    d["life"] = e.life;
    d["water"] = e.water;
    d["poisoned"] = e.poisoned;
    return d;
}

bool DcWalker::assign_quick(int quick_slot, int bag_slot) {
    if (!world_ || quick_slot < 0 || quick_slot > 2 || bag_slot < 0 || bag_slot >= dccore::kBagSlots) return false;
    dccore::UserStatus &st = world_->status;
    const int id = st.get<int16_t>(0x436e + size_t(bag_slot) * 2);
    if (id < 0x84 || id > 0x100) return false;
    const size_t id_at = 0x4362 + size_t(quick_slot) * 2, count_at = 0x4368 + size_t(quick_slot) * 2;
    const int old_id = st.get<int16_t>(id_at);
    if (old_id == id) return false;
    if (old_id != -1) {   // the old stack goes back into the bag, one entry each
        int back = st.get<int16_t>(count_at);
        for (int i = 0; i < dccore::kBagSlots && back > 0; ++i)
            if (st.get<int16_t>(0x436e + size_t(i) * 2) == -1) {
                st.set<int16_t>(0x436e + size_t(i) * 2, int16_t(old_id));
                --back;
            }
    }
    int n = 0;
    for (int i = 0; i < dccore::kBagSlots; ++i)
        if (st.get<int16_t>(0x436e + size_t(i) * 2) == id) {
            st.set<int16_t>(0x436e + size_t(i) * 2, -1);
            st.set<int16_t>(0x443c + size_t(i) * 2, 0);
            ++n;
        }
    st.set<int16_t>(id_at, int16_t(id));
    st.set<int16_t>(count_at, int16_t(n));
    return true;
}

Array DcWalker::get_weapons() const {
    Array out;
    if (!world_) return out;
    const dccore::UserStatus &st = world_->status;
    const int ch = st.character();
    for (int i = 0; i < 11; ++i) {
        const size_t at = 0x450c + size_t(ch) * 0xaa8 + size_t(i) * 0xf8;
        if (st.get<int16_t>(at) < 0x101) continue;
        const dccore::WeaponView w{st.raw.data() + at};
        Dictionary d;
        d["slot"] = i;
        d["id"] = int(w.id());
        d["level"] = int(w.level());
        d["attack"] = int(w.attack());
        d["durability"] = w.durability();
        d["max_durability"] = int(w.max_durability());
        d["exp"] = int(w.exp());
        d["current"] = st.current_weapon_slot(ch) == i;
        out.push_back(d);
    }
    return out;
}

bool DcWalker::equip_weapon(int slot) {
    if (!world_ || slot < 0 || slot >= 11) return false;
    const int ch = world_->status.character();
    if (world_->status.get<int16_t>(0x450c + size_t(ch) * 0xaa8 + size_t(slot) * 0xf8) < 0x101) return false;
    world_->status.set<int8_t>(0x4340 + size_t(ch), int8_t(slot));
    return true;
}

// The shot effects: every instance is the effect's own model (a small character with motions) at the instance's position, turned along its flight if the
// effect says so. The slingshot's bullets, the machine gun's bullets and the flame blobs have no model of their own here: small glowing spheres.
PackedFloat32Array DcWalker::coop_export_fx() const {
    PackedFloat32Array out;
    if (!world_ || !kit_) return out;
    const int ch = world_->status.character();
    const dccore::WeaponView w = world_->status.weapon(ch);
    std::vector<std::array<float, 9>> inst;
    std::vector<std::array<float, 5>> pts;
    bool look = false;
    if (world_->player_fx >= 0) {
        const dccore::ShotEffect &fx = world_->shots.slot[size_t(world_->player_fx)];
        look = fx.def.look_at != 0;
        for (const dccore::ShotEffect::Instance &s : fx.inst) {
            if (!s.active) continue;
            float p[3] = {s.pos[0], s.pos[1], s.pos[2]};
            if (s.attach && walker_) for (int k = 0; k < 3; ++k) p[k] = walker_->weapon_origin[k];
            inst.push_back({p[0], p[1], p[2], s.vel[0], s.vel[1], s.vel[2], float(s.ms.cur), s.ms.time, 0.0f});
        }
    }
    for (const auto &b : world_->bullets)
        if (b.active) pts.push_back({b.pos[0], b.pos[1], b.pos[2], 1.2f, 0.0f});
    for (const auto &b : world_->mg)
        if (b.state > 0) pts.push_back({b.pos[0], b.pos[1], b.pos[2], 0.9f, 1.0f});
    for (const auto &b : world_->flame)
        if (b.state == 0) pts.push_back({b.pos[0], b.pos[1], b.pos[2], std::max(1.0f, b.size * 0.5f), 2.0f});
    if (inst.size() > 8) inst.resize(8);
    if (pts.size() > 40) pts.resize(40);
    out.push_back(float(ch));
    out.push_back(float(w.id()));
    out.push_back(float(w.element_choice()));
    out.push_back(look ? 1.0f : 0.0f);
    out.push_back(float(inst.size()));
    out.push_back(float(pts.size()));
    for (const auto &i : inst) for (float v : i) out.push_back(v);
    for (const auto &p : pts) for (float v : p) out.push_back(v);
    return out;
}

void DcWalker::coop_apply_fx(int peer, const PackedFloat32Array &d) {
    if (d.size() < 6) return;
    RemoteFx &r = remote_fx_[peer];
    const float *f = d.ptr();
    const int ch = int(f[0]), weapon = int(f[1]), element = int(f[2]);
    const size_t ni = size_t(std::max(0, int(f[4]))), np = size_t(std::max(0, int(f[5])));
    if (size_t(d.size()) < 6 + ni * 9 + np * 5) return;
    if ((r.character != ch || r.weapon != weapon || r.element != element) && weapon_changer_) {   // (his weapon changed: the effect model of the new one)
        r.character = ch;
        r.weapon = weapon;
        r.element = element;
        r.model.reset();
        if (auto kit = weapon_changer_(ch, weapon, element)) r.model = kit->has_main_fx ? kit->main_fx_model : nullptr;
        for (DcCharacter *n : r.chars) if (n) n->queue_free();
        r.chars.clear();
    }
    r.look_at = f[3] != 0.0f;
    r.inst.assign(ni, {});
    r.pts.assign(np, {});
    int at = 6;
    for (size_t i = 0; i < ni; ++i, at += 9) for (int k = 0; k < 9; ++k) r.inst[i][size_t(k)] = f[at + k];
    for (size_t i = 0; i < np; ++i, at += 5) for (int k = 0; k < 5; ++k) r.pts[i][size_t(k)] = f[at + k];
    r.age = 0;
}

void DcWalker::coop_clear_fx(int peer) {
    auto it = remote_fx_.find(peer);
    if (it == remote_fx_.end()) return;
    for (DcCharacter *n : it->second.chars) if (n) n->queue_free();
    for (MeshInstance3D *n : it->second.dots) if (n) n->queue_free();
    remote_fx_.erase(it);
}

void DcWalker::apply_remote_fx() {
    for (auto &kv : remote_fx_) {
        RemoteFx &r = kv.second;
        if (++r.age > 30) { r.inst.clear(); r.pts.clear(); }   // (his data stopped coming: nothing of his effect is left)
        if (r.chars.size() < r.inst.size()) r.chars.resize(r.inst.size(), nullptr);
        for (size_t i = 0; i < r.chars.size(); ++i) {
            DcCharacter *&node = r.chars[i];
            if (i >= r.inst.size() || !r.model) { if (node) node->set_visible(false); continue; }
            if (!node) {
                node = memnew(DcCharacter);
                Dictionary info;
                node->setup(r.model, info);
                node->set_playing(false);
                node->set_effect_look(true);
                add_child(node);
            }
            const auto &s = r.inst[i];
            node->set_visible(true);
            Transform3D t;
            t.origin = Vector3(s[0], s[1], s[2]);
            const Vector3 dir(s[3], s[4], s[5]);
            if (r.look_at && dir.length_squared() > 1e-8f) {
                const Vector3 z = dir.normalized();
                Vector3 up(0, 1, 0);
                if (std::abs(z.dot(up)) > 0.999f) up = Vector3(1, 0, 0);
                const Vector3 x = up.cross(z).normalized();
                t.basis = Basis(x, z.cross(x), z);
            }
            node->set_transform(t);
            if (node->get_motion() != int(s[6])) node->play(int(s[6]), false);
            node->set_time(s[7]);
        }
        if (r.dots.size() < r.pts.size()) {
            for (size_t i = r.dots.size(); i < r.pts.size(); ++i) {
                MeshInstance3D *m = memnew(MeshInstance3D);
                Ref<SphereMesh> sph;
                sph.instantiate();
                sph->set_radius(1.0);
                sph->set_height(2.0);
                m->set_mesh(sph);
                Ref<StandardMaterial3D> mat;
                mat.instantiate();
                mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
                m->set_material_override(mat);
                add_child(m);
                r.dots.push_back(m);
            }
        }
        for (size_t i = 0; i < r.dots.size(); ++i) {
            if (i >= r.pts.size()) { r.dots[i]->set_visible(false); continue; }
            const auto &p = r.pts[i];
            r.dots[i]->set_visible(true);
            r.dots[i]->set_position(Vector3(p[0], p[1], p[2]));
            r.dots[i]->set_scale(Vector3(1, 1, 1) * p[3]);
            Ref<StandardMaterial3D> mat = r.dots[i]->get_material_override();
            mat->set_albedo(p[4] == 0.0f ? Color(0.9f, 0.9f, 0.7f) : (p[4] == 1.0f ? Color(1.0f, 0.85f, 0.3f) : Color(1.0f, 0.45f, 0.1f)));
        }
    }
}

void DcWalker::apply_shots() {
    apply_remote_fx();
    if (!world_) return;
    for (size_t si = 0; si < world_->shots.slot.size(); ++si) {
        const dccore::ShotEffect &fx = world_->shots.slot[si];
        for (size_t ii = 0; ii < fx.inst.size(); ++ii) {
            const size_t key = si * size_t(dccore::ShotEffect::kInstances) + ii;
            if (key >= shot_chars_.size()) shot_chars_.resize(key + 1, nullptr);
            DcCharacter *&node = shot_chars_[key];
            const dccore::ShotEffect::Instance &s = fx.inst[ii];
            if (!fx.loaded || !s.active || !fx.model) {
                if (node) node->set_visible(false);
                continue;
            }
            if (!node || node->get_character() != fx.model) {   // (a different effect now uses this slot)
                if (node) node->queue_free();
                node = memnew(DcCharacter);
                Dictionary info;
                node->setup(fx.model, info);
                node->set_playing(false);
                node->set_effect_look(true);
                add_child(node);
            }
            node->set_visible(true);
            Vector3 pos(s.pos[0], s.pos[1], s.pos[2]);
            if (s.attach && walker_) pos = Vector3(walker_->weapon_origin[0], walker_->weapon_origin[1], walker_->weapon_origin[2]);
            Transform3D t;
            t.origin = pos;
            const Vector3 dir(s.vel[0], s.vel[1], s.vel[2]);
            if (fx.def.look_at != 0 && dir.length_squared() > 1e-8f) {   // LookAtMatrixZ: the model's +Z runs along the flight
                const Vector3 z = dir.normalized();
                Vector3 up(0, 1, 0);
                if (std::abs(z.dot(up)) > 0.999f) up = Vector3(1, 0, 0);
                const Vector3 x = up.cross(z).normalized();
                const Vector3 y = z.cross(x);
                t.basis = Basis(x, y, z);
            }
            node->set_transform(t);
            if (node->get_motion() != s.ms.cur) node->play(s.ms.cur, false);
            node->set_time(s.ms.time);
        }
    }
    // small spheres for the projectiles that have no effect model
    int n = 0;
    auto sphere = [&](const float p[3], float radius, const Color &col) {
        if (size_t(n) >= shot_nodes_.size()) {
            MeshInstance3D *m = memnew(MeshInstance3D);
            Ref<SphereMesh> sph;
            sph.instantiate();
            sph->set_radius(1.0);
            sph->set_height(2.0);
            m->set_mesh(sph);
            Ref<StandardMaterial3D> mat;
            mat.instantiate();
            mat->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
            m->set_material_override(mat);
            add_child(m);
            shot_nodes_.push_back(m);
        }
        MeshInstance3D *node = Object::cast_to<MeshInstance3D>(shot_nodes_[size_t(n)]);
        Ref<StandardMaterial3D> mat = node->get_material_override();
        mat->set_albedo(col);
        node->set_visible(true);
        node->set_position(Vector3(p[0], p[1], p[2]));
        node->set_scale(Vector3(1, 1, 1) * radius);
        ++n;
    };
    for (const auto &b : world_->bullets)
        if (b.active) sphere(b.pos, 1.2f, Color(0.9f, 0.9f, 0.7f));
    for (const auto &b : world_->mg)
        if (b.state > 0) sphere(b.pos, 0.9f, Color(1.0f, 0.85f, 0.3f));
    for (const auto &b : world_->flame)
        if (b.state == 0) sphere(b.pos, std::max(1.0f, b.size * 0.5f), Color(1.0f, 0.45f, 0.1f));
    for (size_t i = size_t(n); i < shot_nodes_.size(); ++i) shot_nodes_[i]->set_visible(false);
}

PackedFloat32Array DcWalker::debug_floor_scan(double x0, double x1, double z0, double z1, double step, double from_y) const {
    PackedFloat32Array out;
    if (!dungeon_ || step <= 0.0) return out;
    for (double z = z0; z <= z1; z += step)
        for (double x = x0; x <= x1; x += step) {
            const std::vector<dccore::CCPoly> polys = dccore::floor_polys_near(*dungeon_, layout_, float(x), float(z), 1);
            const float p[3] = {float(x), float(from_y), float(z)};
            float hit[3];
            out.push_back(dccore::check_hit_vertical(-2000.0f, polys.data(), int(polys.size()), p, hit, 0) >= 0 ? hit[1] : -9999.0f);
        }
    return out;
}

// Thrown items: the reticle while aiming (a flat disc where the item will land), a small ball per item in flight and a flash where one exploded (kind 32 notice: item id, position).
void DcWalker::apply_throw() {
    if (!world_ || !walker_) return;
    for (const auto &b : world_->blasts) {
        Dictionary d;
        d["kind"] = 32;
        d["a"] = b.id;
        d["b"] = Vector3(b.pos[0], b.pos[1], b.pos[2]);
        notice_queue_.push_back(d);
        MeshInstance3D *mi = memnew(MeshInstance3D);
        Ref<SphereMesh> sm;
        sm.instantiate();
        sm->set_radius(1.0f);
        sm->set_height(2.0f);
        mi->set_mesh(sm);
        Ref<StandardMaterial3D> m;
        m.instantiate();
        m->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
        m->set_blend_mode(BaseMaterial3D::BLEND_MODE_ADD);
        m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        static const Color elem[5] = {Color(1.0f, 0.45f, 0.1f), Color(0.5f, 0.8f, 1.0f), Color(1.0f, 1.0f, 0.4f), Color(0.6f, 1.0f, 0.7f), Color(1.0f, 1.0f, 0.9f)};
        const bool gem = b.id >= 0xa1 && b.id <= 0xa5;
        m->set_albedo(gem ? elem[b.id - 0xa1] : (b.id == 0x98 ? elem[4] : Color(1.0f, 0.6f, 0.2f)));
        mi->set_material_override(m);
        mi->set_position(Vector3(b.pos[0], b.pos[1], b.pos[2]));
        mi->set_scale(Vector3(1, 1, 1) * 2.0f);
        add_child(mi);
        const bool bomb = b.id == 0x9f || b.id == 0xa8;
        blast_nodes_.push_back({mi, 0, bomb ? 26.0f : (gem || b.id == 0x98 ? 22.0f : 9.0f)});
    }
    world_->blasts.clear();
    for (const auto &s : world_->sounds) {   // monsters' sounds (kind 33 notice: id, position; b = boss, stop)
        Dictionary d;
        d["kind"] = 33;
        d["a"] = s.id;
        d["b"] = Vector3(s.pos[0], s.pos[1], s.pos[2]);
        d["boss"] = s.boss;
        d["stop"] = s.stop;
        notice_queue_.push_back(d);
        if (coop_role_ == 1) coop_sound_out_.push_back(d);
    }
    world_->sounds.clear();
    for (size_t i = 0; i < blast_nodes_.size();) {
        BlastNode &bn = blast_nodes_[i];
        ++bn.age;
        const float f = float(bn.age) / 24.0f;
        bn.node->set_scale(Vector3(1, 1, 1) * (2.0f + bn.size * f));
        Ref<StandardMaterial3D> m = bn.node->get_material_override();
        if (m.is_valid()) { Color c = m->get_albedo(); c.a = std::max(0.0f, 0.85f * (1.0f - f)); m->set_albedo(c); }
        if (bn.age >= 24) {
            bn.node->queue_free();
            blast_nodes_.erase(blast_nodes_.begin() + long(i));
        } else {
            ++i;
        }
    }
    // the items in flight
    int n = 0;
    for (const auto &t : world_->thrown) {
        if (!t.active) continue;
        if (size_t(n) >= thrown_nodes_.size()) {
            MeshInstance3D *mi = memnew(MeshInstance3D);
            Ref<SphereMesh> sm;
            sm.instantiate();
            sm->set_radius(1.0f);
            sm->set_height(2.0f);
            mi->set_mesh(sm);
            Ref<StandardMaterial3D> m;
            m.instantiate();
            mi->set_material_override(m);
            add_child(mi);
            thrown_nodes_.push_back(mi);
        }
        MeshInstance3D *mi = thrown_nodes_[size_t(n)];
        Ref<StandardMaterial3D> m = mi->get_material_override();
        const bool bomb = t.id == 0x9f || t.id == 0xa8;
        m->set_albedo(bomb ? Color(0.15f, 0.15f, 0.18f) : (t.id == 0xa0 ? Color(0.5f, 0.5f, 0.5f) : Color(0.9f, 0.5f, 0.6f)));
        mi->set_visible(true);
        mi->set_position(Vector3(t.pos[0], t.pos[1], t.pos[2]));
        mi->set_scale(Vector3(1, 1, 1) * (bomb ? 3.0f : 2.0f));
        ++n;
    }
    for (size_t i = size_t(n); i < thrown_nodes_.size(); ++i) thrown_nodes_[i]->set_visible(false);
    // the reticle
    if (!reticle_node_) {
        reticle_node_ = memnew(MeshInstance3D);
        Ref<CylinderMesh> cm;
        cm.instantiate();
        cm->set_top_radius(1.0f);
        cm->set_bottom_radius(1.0f);
        cm->set_height(0.4f);
        reticle_node_->set_mesh(cm);
        Ref<StandardMaterial3D> m;
        m.instantiate();
        m->set_shading_mode(StandardMaterial3D::SHADING_MODE_UNSHADED);
        m->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
        m->set_albedo(Color(1.0f, 0.5f, 0.1f, 0.6f));
        reticle_node_->set_material_override(m);
        add_child(reticle_node_);
    }
    const bool aim = walker_->throw_aim != 0 && walker_->combat.mode == 2;
    reticle_node_->set_visible(aim);
    if (aim) {
        reticle_node_->set_position(Vector3(walker_->reticle[0], walker_->reticle[1] + 0.5f, walker_->reticle[2]));
        reticle_node_->set_scale(Vector3(6, 1, 6));
    }
}

void DcWalker::make_chest_node(const dccore::Chest &c) {
    MeshInstance3D *m = memnew(MeshInstance3D);
    Ref<BoxMesh> box;
    box.instantiate();
    box->set_size(c.small ? Vector3(14, 9, 9) : Vector3(18, 12, 11));
    m->set_mesh(box);
    Ref<StandardMaterial3D> mat;
    mat.instantiate();
    mat->set_albedo(c.small ? Color(0.55f, 0.32f, 0.12f) : Color(0.85f, 0.7f, 0.15f));
    m->set_material_override(mat);
    m->set_position(Vector3(c.pos[0], c.pos[1] + 4.5f, c.pos[2]));
    add_child(m);
    chest_nodes_.push_back(m);
}

// ---- the dungeon's story script ---------------------------------------------------------------------------------------------------------------------------

void DcWalker::set_dungeon_session(std::shared_ptr<dccore::DungeonSession> s, int floor) {
    session_ = std::move(s);
    if (!session_) return;
    floor_start_pending_ = floor;
    session_->set_event_sw = [this](int id, bool on) {
        if (!events_) return;
        for (size_t i = 0; i < events_->markers.size(); ++i) {
            if (events_->markers[i].marker.event_id != id) continue;
            events_->marker_state[i].on = on;
            if (!on && (id == 360 || id == 361 || id == 365 || id == 366 || id == 370)) {   // a character room was solved: its door (cdoor*) stays open for the floor
                const int cx = events_->markers[i].cell_x, cy = events_->markers[i].cell_y;
                if (size_t(cy * dccore::kGrid + cx) < layout_.door_open.size()) layout_.door_open[size_t(cy * dccore::kGrid + cx)] |= 4;
                if (floor_) floor_->open_door(cx, cy, 4);
            }
        }
    };
    session_->spawn_monster = [this](int k, bool load, const float *pos, int death_event) { return spawn_script_monster(k, load, pos, death_event); };
    session_->place_item_box = [this](const float pos[3], int item) {
        if (!events_) return;
        const float p4[4] = {pos[0], pos[1], pos[2], 1.0f};
        events_->ev.set_chest(p4, item, item < 0x101, 0);
    };
    session_->monster_count = [this]() {
        int n = 0;
        if (world_ && monsters_)
            for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i)
                if (monsters_->slots[size_t(i)].active && world_->actors[size_t(i)].state != -1) ++n;
        return n;
    };
    session_->take_death_event = [this]() {
        if (!world_) return -1;
        for (auto &a : world_->actors)
            if (a.state == 2 && a.death_event_pending != 0) {
                a.death_event_pending = 0;
                return int(a.death_event);
            }
        return -1;
    };
    session_->hold_world = [this](bool hold) { event_hold_ = hold; };
    session_->hero_state = [this](float pos[3], float &yaw) {
        for (int k = 0; k < 3; ++k) pos[k] = walker_->player.pos[k];
        yaw = walker_->player.yaw;
    };
    session_->place_hero = [this](const float pos[3], float yaw) {
        if (world_ && world_->trace) world_->log.push_back("place hero " + std::to_string(pos[0]) + "," + std::to_string(pos[1]) + "," + std::to_string(pos[2]));
        walker_->place_at(pos[0], pos[2], pos[1] + 4.0f);
        walker_->player.yaw = yaw;
    };
    session_->rt().trace = [this](int id, const char *name, const dccore::ScriptArgs &a) { if (world_ && world_->trace) { std::string t = "ev ext " + std::to_string(id) + " " + name; if (id == 192 || id == 193) for (int i = 0; i < a.n && i < 4; ++i) t += " " + std::to_string(a.get_int(i)); if (id == 137 || id == 7) for (int i = 0; i < a.n && i < 4; ++i) t += " " + std::to_string(a.get_float(i)); world_->log.push_back(t); } };
    session_->gate_key_item = [this]() { return events_ ? events_->gate_key_item : 0; };
    session_->ruby_key = ruby_key_;
    session_->find_frame = [this](const std::string &name, float pos[3], float &yaw) {
        if (!dungeon_) return false;
        if (!dccore::find_floor_frame(*dungeon_, layout_, name, pos, &yaw)) return false;
        // the gaps: the hero is sent to the other end, so the heading of the frame points at its partner
        static const char *kPairs[][2] = {{"chr1_3", "chr1_4"}, {"chr1_4", "chr1_3"}, {"chr5_0", "chr5_1"}, {"chr5_1", "chr5_0"}};
        for (const auto &pr : kPairs)
            if (name == pr[0]) {
                float q[3], y2;
                if (dccore::find_floor_frame(*dungeon_, layout_, pr[1], q, &y2)) yaw = std::atan2(q[0] - pos[0], q[2] - pos[2]);
            }
        return true;
    };
    session_->item_trap_id = [this]() { return events_ && ibox_ >= 0 && size_t(ibox_) < events_->ev.chests.size() ? events_->ev.chests[size_t(ibox_)].quality : 0; };
    session_->reset_item_trap = [this]() {
        if (events_ && ibox_ >= 0 && size_t(ibox_) < events_->ev.chests.size()) events_->ev.chests[size_t(ibox_)].quality = 0;
        ibox_ = 0;
    };
    session_->bomb_set = [this]() {
        if (!world_ || !walker_) return;
        const float p[3] = {walker_->player.pos[0], walker_->player.pos[1] + 16.0f, walker_->player.pos[2] - 3.0f};
        world_->trap_bomb(p);
    };
    session_->status_err = [this](int code) {
        if (!world_ || !walker_) return;
        walker_->apply_status_err(code);
        const float p[3] = {walker_->player.pos[0], walker_->player.pos[1] + 16.0f, walker_->player.pos[2]};
        world_->trap_hurt(p);
    };
    session_->ibox_angle = [this](float angle) {   // the lid: the box node tilts open
        if (events_ && ibox_ >= 0 && size_t(ibox_) < chest_nodes_.size() && chest_nodes_[size_t(ibox_)]) chest_nodes_[size_t(ibox_)]->set_rotation_degrees(Vector3(angle * 0.25f, 0, 0));
    };
    session_->ibox_finish = [this]() {   // the clown's chest is used up
        if (!events_ || ibox_ < 0 || size_t(ibox_) >= events_->ev.chests.size()) return;
        dccore::Chest &c = events_->ev.chests[size_t(ibox_)];
        c.state = 1;
        c.active = false;
    };
    session_->piero_item = [this](int out[2]) {
        out[0] = out[1] = -1;
        if (event_tables_) event_tables_->piero_item(ev_map_, in_back_ ? 1 : 0, ev_floor_, rng_, out);
    };
    session_->set_ura_dungeon = [this]() { if (back_.present) ura_pending_ = true; };
    session_->hold_item_event = [this](int id) {   // SearchItemEventHold on both event managers
        for (dccore::EventWorld *w : {events_.get(), shelf_.events.get()}) {
            if (!w) continue;
            for (size_t i = 0; i < w->markers.size(); ++i)
                if (w->markers[i].marker.event_id == id) w->marker_state[i].item_use = false;
        }
    };
    session_->floor_title = [this]() {
        Dictionary d;
        d["kind"] = 42;
        d["a"] = in_back_ ? 1 : 0;
        notice_queue_.push_back(d);
    };
    session_->move_frame = [this](const std::string &name, const float off[3], const float rot[3]) {
        frame_moves_[name] = Vector3(off[0], off[1], off[2]);
        layout_.frame_moves[name] = {off[0], off[1], off[2]};
        if (floor_) floor_->set_object(String(name.c_str()), Vector3(off[0], off[1], off[2]), Vector3(rot[0], rot[1], rot[2]));
    };
    session_->frame_draw = [this](const std::string &name, bool show) {
        frame_shown_[name] = show;
        if (floor_) floor_->set_object_visible(String(name.c_str()), show);
    };
}

int DcWalker::spawn_script_monster(int k, bool load, const float *pos, int death_event) {
    if (world_ && world_->trace) world_->log.push_back("script spawn k " + std::to_string(k) + " load " + std::to_string(load));
    if (!world_ || !monsters_ || k < 0 || size_t(k) >= monsters_->bases.size()) return -1;
    if (load)   // CleanViewMonstor: everything that stands on the floor goes
        for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i) {
            monsters_->slots[size_t(i)].active = false;
            world_->actors[size_t(i)].state = -1;
        }
    int slot = -1;
    for (int i = 0; i < dccore::MonsterUnit::kSlots; ++i)
        if (!monsters_->slots[size_t(i)].active) { slot = i; break; }
    if (slot < 0) return -1;
    dccore::MonsterInstance &mi = monsters_->slots[size_t(slot)];
    mi.active = true;
    mi.base = k;
    mi.pos[0] = pos ? pos[0] : 0.0f;
    mi.pos[1] = pos ? pos[1] : 0.0f;
    mi.pos[2] = pos ? pos[2] : 0.0f;
    world_->spawn(slot);
    for (int j = 0; j < 3; ++j) mi.pos[j] = world_->actors[size_t(slot)].pos[j];
    world_->actors[size_t(slot)].death_event = int16_t(death_event);
    world_->actors[size_t(slot)].death_event_pending = 0;
    return slot;
}

// MoveChara's marker polling for the markers whose events are scripts (the stairs, gates and doors have their own code in EventWorld).
void DcWalker::poll_script_markers(const dccore::PadRaw &pad, bool ok_down) {
    if (!session_ || !events_ || session_->running()) return;
    const bool use_down = (pad.buttons & dccore::kPadUse) != 0 && (session_prev_buttons_ & dccore::kPadUse) == 0;
    const float *pp = walker_->player.pos;
    for (size_t i = 0; i < events_->markers.size(); ++i) {
        dccore::MarkerState &st = events_->marker_state[i];
        const dccore::FloorMarker &fm = events_->markers[i];
        const int id = fm.marker.event_id;
        if (!st.on) { if (i < script_inside_.size()) script_inside_[i] = false; continue; }
        // (the stairs, the gate and the doors are handled by EventWorld; their script functions do not exist, has_function below skips them)
        if (!session_->has_function(id)) continue;
        if ((id == 200 || id == 201) && !back_.present) continue;
        if (world_ && world_->trace) world_->log.push_back("marker " + std::to_string(id) + " y " + std::to_string(fm.pos[1]) + " auto " + std::to_string(fm.marker.link));
        const float dx = pp[0] - fm.pos[0], dz = pp[2] - fm.pos[2];
        const bool inside = std::fabs(fm.pos[1] - pp[1]) < 40.0f && std::sqrt(dx * dx + dz * dz) <= fm.marker.radius + 10.0f;
        if (script_inside_.size() <= i) script_inside_.resize(events_->markers.size(), false);
        const bool entered = inside && !script_inside_[i];
        if (world_ && world_->trace && entered) world_->log.push_back("marker entered " + std::to_string(id));
        script_inside_[i] = inside;
        if (!inside) continue;
        if (fm.marker.hit) {   // a character-room marker: OK only asks for a hint (the hit itself is polled after the player's step)
            if (!ok_down) continue;
            if (hint_char_.size() <= i) hint_char_.resize(events_->markers.size(), -1);
            const int me = world_ ? world_->status.character() : 0;
            if (fm.marker.chara != -1 && hint_char_[i] == me) continue;   // he has read it: his OK is an attack now
            if (me == fm.marker.chara) hint_char_[i] = me;
            session_->start(id, 1, fm.pos, fm.marker.extra, 1);
            if (session_->running()) break;
            continue;
        }
        if (fm.marker.link) {            // an automatic area starts when it is stepped on
            if (entered) session_->start(id, 0, fm.pos, fm.marker.extra);
        } else if (use_down && st.item_use) {
            session_->start(id, 2, fm.pos, fm.marker.extra);
        } else if (ok_down) {
            session_->start(id, 1, fm.pos, fm.marker.extra);
        }
        if (session_->running()) break;
    }
}

// The hit markers: a sphere of the player's pool (owner kind = the character, sub id = the attack: Ungaga's blast 2, Ruby's element) inside the marker's radius starts the event.
void DcWalker::poll_hit_markers() {
    if (!session_ || !events_ || !world_ || session_->running()) return;
    for (size_t i = 0; i < events_->markers.size(); ++i) {
        const dccore::FloorMarker &fm = events_->markers[i];
        if (!fm.marker.hit || !events_->marker_state[i].on || !session_->has_function(fm.marker.event_id)) continue;
        const int attack = fm.marker.attack == -2 ? ruby_key_ : fm.marker.attack;
        for (const dccore::ColEntry &e : world_->pool.e) {
            if (!e.used || e.owner_kind != fm.marker.chara) continue;
            if (world_->trace) world_->log.push_back("hitpoll owner " + std::to_string(e.owner_kind) + " sub " + std::to_string(e.sub_id) + " want " + std::to_string(attack) + " pos " + std::to_string(e.pos[0]) + "," + std::to_string(e.pos[1]) + "," + std::to_string(e.pos[2]) + " marker " + std::to_string(fm.pos[0]) + "," + std::to_string(fm.pos[1]) + "," + std::to_string(fm.pos[2]));
            if (attack != -1 && e.sub_id != attack) continue;
            const float dx = e.pos[0] - fm.pos[0], dy = e.pos[1] - fm.pos[1], dz = e.pos[2] - fm.pos[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) > fm.marker.radius + e.radius) continue;   // (the sphere's own size counts: Ruby's crystals have a radius of 3 and shots fly 5 units off the frame)
            session_->start(fm.marker.event_id, 1, fm.pos, fm.marker.extra, 0);
            return;
        }
    }
}

Dictionary DcWalker::get_event_ui() const {
    Dictionary d;
    d["running"] = session_ && session_->running();
    if (!session_) return d;
    dccore::EventRuntime &rt = session_->rt();
    Array wins;
    for (size_t w = 0; w < rt.windows.size(); ++w) {
        const dccore::MesWindow &win = rt.windows[w];
        if (win.state() == dccore::MesWindow::Closed) continue;
        Dictionary wd;
        wd["window"] = int(w);
        wd["text"] = String::utf8(win.visible_text().c_str());
        wd["state"] = win.state();
        wd["waiting"] = win.waiting_key || win.state() == dccore::MesWindow::PageWait || win.state() == dccore::MesWindow::Complete;
        wins.push_back(wd);
    }
    d["windows"] = wins;
    d["fade_alpha"] = rt.fade.alpha / 128.0f;
    d["fade_color"] = Color(float(rt.fade.r) / 255.0f, float(rt.fade.g) / 255.0f, float(rt.fade.b) / 255.0f);
    if (rt.system_message.active) d["system_message"] = String::utf8(rt.system_message.text.c_str());
    return d;
}

// ---- the stage of the story events: the models the script loads, the actors that move, the camera ------------------------------------------------------------

namespace {
class DungeonEventHost : public dccore::EventHost {
public:
    DcWalker *w;
    explicit DungeonEventHost(DcWalker *walker) : w(walker) {}
    bool load_pack(int index, const std::string &path) override { return w->ev_load_pack(index, path); }
    bool load_actor(int slot, int pack, const std::string &entry) override { return w->ev_load_actor(slot, pack, entry); }
    void unload_actor(int slot) override { w->ev_unload_actor(slot); }
    bool actor_motion_info(int slot, int motion, float &start, float &end, float &blend) override {
        blend = 2.0f;
        return w->ev_motion_info(slot, motion, start, end);
    }
    float actor_motion_time(int slot) override { return w->ev_motion_time(slot); }
    bool actor_motion_range(int slot, int motion, float &start, float &end, float &speed) override { return w->ev_motion_range(slot, motion, start, end, speed); }
    void open_modal(int kind, const std::vector<int> &args) override { w->open_item_modal(kind, args); }
    void sound(const char *op, int a, int, const float *) override {   // (the EB duel's sounds: the front end plays them)
        const std::string o = op;
        if (o.rfind("_EB_", 0) != 0) return;
        Dictionary d;
        d["kind"] = 34;
        d["op"] = String(op);
        d["a"] = a;
        w->coop_show_notice(d);
    }
};
}  // namespace

void DcWalker::open_item_modal(int kind, const std::vector<int> &args) {
    if (kind != 11) { modal_done(0); return; }
    if (auto_item_pick_ || !world_) {   // (no window: the first listed item the player has, else none)
        int found = -1;
        const Array bag = get_bag();
        for (int item : args) {
            if (found >= 0 || item < 0) continue;
            for (int k = 0; k < bag.size() && found < 0; ++k)
                if (int(Dictionary(bag[k])["id"]) == item) found = item;
        }
        modal_done(found);
        return;
    }
    Dictionary d;
    d["kind"] = 43;
    Array list;
    for (int item : args) list.push_back(item);
    d["args"] = list;
    notice_queue_.push_back(d);
}

dccore::EventHost *DcWalker::event_host() {
    if (!ev_host_) ev_host_ = std::make_unique<DungeonEventHost>(this);
    return ev_host_.get();
}

bool DcWalker::ev_load_pack(int index, const std::string &path) {
    if (index < 0 || index >= 16) return false;
    if (ev_pack_paths_.size() <= size_t(index)) ev_pack_paths_.resize(size_t(index) + 1);
    ev_pack_paths_[size_t(index)] = path;
    if (!ev_packs_.count(path)) ev_packs_[path] = pack_loader ? pack_loader(path) : nullptr;
    return ev_packs_[path] != nullptr;
}

bool DcWalker::ev_load_actor(int slot, int pack, const std::string &entry) {
    if (pack < 0 || size_t(pack) >= ev_pack_paths_.size()) return false;
    auto it = ev_packs_.find(ev_pack_paths_[size_t(pack)]);
    if (it == ev_packs_.end() || !it->second) return false;
    try {
        if (const dccore::PackEntry *cfg = it->second->find(entry)) {
            const dccore::View text = it->second->data(*cfg);
            if (!dccore::cfg_has_model(text)) {   // a config that only adds motions to the model already there (the EB duel's hero, jump scenes ...)
                DcCharacter *target = nullptr;
                auto have = ev_actor_nodes_.find(slot);
                if (have != ev_actor_nodes_.end()) target = have->second;
                else if (slot == -1) target = player_;
                if (target && target->get_character()) {
                    auto copy = std::make_shared<dccore::Character>(*target->get_character());
                    dccore::add_motion_sets(*copy, *it->second, text);
                    target->set_character(copy);
                }
                return true;
            }
        }
        auto ch = std::make_shared<dccore::Character>(dccore::load_character(*it->second, entry));
        if (ch->model_name.empty()) return true;
        DcCharacter *node = memnew(DcCharacter);
        Dictionary info;
        node->setup(ch, info);
        add_child(node);
        auto old = ev_actor_nodes_.find(slot);
        if (old != ev_actor_nodes_.end()) { remove_child(old->second); old->second->queue_free(); }
        ev_actor_nodes_[slot] = node;
        if (slot == -1 && player_) player_->set_visible(false);
        return true;
    } catch (const std::exception &) {
        return true;
    }
}

void DcWalker::ev_unload_actor(int slot) {
    auto it = ev_actor_nodes_.find(slot);
    if (it == ev_actor_nodes_.end()) return;
    remove_child(it->second);
    it->second->queue_free();
    ev_actor_nodes_.erase(it);
}

bool DcWalker::ev_motion_info(int slot, int motion, float &start, float &end) {
    DcCharacter *n = nullptr;
    auto it = ev_actor_nodes_.find(slot);
    if (it != ev_actor_nodes_.end()) n = it->second;
    else if (slot == -1) n = player_;
    start = 0.0f;
    end = 10.0f;
    if (!n) return true;   // (an actor without a model: the motion is over at once)
    const Dictionary d = n->get_motion_info(motion);
    if (!d.has("end")) return true;
    start = float(double(d["start"]));
    end = float(double(d["end"]));
    return true;
}

float DcWalker::ev_motion_time(int slot) {
    auto c = ev_clock_.find(slot);
    if (c != ev_clock_.end()) return float(c->second.time);
    return 9.995f;
}

// The motion of a stage actor: a request restarts the clock at the motion's first frame; every tick it runs on by the speed (looping unless the flags say hold).
void DcWalker::step_stage_actor(int slot, DcCharacter *node, const dccore::EvActor &a) {
    StageClock &c = ev_clock_[slot];
    if (ev_actor_serial_[slot] != a.motion_serial) {
        ev_actor_serial_[slot] = a.motion_serial;
        const Dictionary mi = node->get_motion_info(a.motion);
        c.start = mi.has("start") ? double(mi["start"]) : 0.0;
        c.end = mi.has("end") ? double(mi["end"]) : 0.0;
        const double table = mi.has("speed") ? double(mi["speed"]) : 1.0;
        c.speed = a.motion_speed > 0.0f ? double(a.motion_speed) : table;
        c.loop = (a.motion_flags & 2) == 0;
        c.time = c.start;
        node->set_playing(false);
        node->play(a.motion, c.loop);
    } else {
        c.time += c.speed;
        if (c.end > c.start && c.time >= c.end) {
            if (c.loop) c.time = c.start + (c.time - c.end);
            else c.time = c.end - 0.001;
        }
    }
    node->set_playing(false);
    node->set_time(c.time);
}

void DcWalker::apply_object_anims() {
    if (!session_ || !floor_) return;
    for (const dccore::EvHandle &hd : session_->rt().handles) {
        if (hd.kind != 6 || !hd.moved) continue;
        for (int bt : hd.bt) {
            const std::string &name = session_->handle_name(bt);
            if (name.empty()) continue;
            floor_->set_object(String(name.c_str()), Vector3(hd.pos.x, hd.pos.y, hd.pos.z), Vector3(hd.rot.x, hd.rot.y, hd.rot.z));
        }
    }
}

void DcWalker::apply_event_stage() {
    if (!session_) return;
    const bool running = session_->running();
    if (!running) {
        if (stage_was_running_) clear_event_stage();
        stage_was_running_ = false;
        return;
    }
    stage_was_running_ = true;
    dccore::EventRuntime &rt = session_->rt();
    for (auto &kv : ev_actor_nodes_) {
        const dccore::EvActor &a = rt.actor(kv.first);
        DcCharacter *node = kv.second;
        node->set_visible(a.draw);
        node->set_position(Vector3(a.pos.x, a.pos.y, a.pos.z));
        node->set_rotation(Vector3(a.rot.x, a.rot.y, a.rot.z));
        node->set_scale(Vector3(a.scale.x, a.scale.y, a.scale.z));
        step_stage_actor(kv.first, node, a);
    }
    if (player_ && !ev_actor_nodes_.count(-1)) {   // the hero of the script is the player's own model
        const dccore::EvActor &h = rt.actor(-1);
        player_->set_visible(h.draw);
        player_->set_position(Vector3(h.pos.x, h.pos.y, h.pos.z));
        player_->set_rotation(Vector3(0, h.rot.y, 0));
        step_stage_actor(-1, player_, h);
    }
}

void DcWalker::clear_event_stage() {
    for (auto &kv : ev_actor_nodes_) {
        remove_child(kv.second);
        kv.second->queue_free();
    }
    ev_actor_nodes_.clear();
    ev_actor_serial_.clear();
    ev_clock_.clear();
    if (player_) {
        player_->set_visible(true);
        if (fighting_) player_->set_playing(false);
    }
}

Dictionary DcWalker::get_hero_pose() const {
    Dictionary d;
    if (!walker_) return d;
    const dccore::PlayerState &p = walker_->player;
    d["pos"] = Vector3(p.pos[0], p.pos[1], p.pos[2]);
    d["yaw"] = p.yaw;
    d["motion"] = fighting_ ? walker_->motion.cur : p.motion;
    d["time"] = fighting_ ? double(walker_->motion.time) : 0.0;
    d["visible"] = !walker_->view_mode && !(session_ && session_->running());
    d["fighting"] = fighting_;
    if (world_) {
        const int ch = world_->status.character();
        d["life"] = int(world_->status.life(ch));
        d["max_life"] = int(world_->status.max_life(ch));
        d["weapon"] = int(world_->status.weapon(ch).id());
    }
    if (fighting_) {
        d["stick"] = Vector2(walker_->combat.stick_dir[0], walker_->combat.stick_dir[1]);
        d["idle"] = walker_->combat.idle_counter;
    }
    return d;
}

// ---- co-op -------------------------------------------------------------------------------------------------------------------------------------------------

void DcWalker::coop_set_role(int role) {
    coop_role_ = role;
    if (world_) world_->coop_client = role == 2;
}

int DcWalker::coop_add_remote(const PackedByteArray &status) {
    if (!world_ || status.size() != int64_t(dccore::UserStatus::kSize)) return 0;
    dccore::UserStatus st;
    std::memcpy(st.raw.data(), status.ptr(), dccore::UserStatus::kSize);
    return world_->add_remote(st);
}

void DcWalker::coop_remove_remote(int id) {
    if (!world_) return;
    world_->remove_remote(id);
    for (auto &e : world_->pool.e)
        if (e.used && e.remote == id) e.used = false;
    imported_.erase(id);
}

void DcWalker::coop_update_remote(int id, const Dictionary &pose) {
    if (!world_) return;
    dccore::RemotePlayer *r = world_->remote(id);
    if (!r) return;
    const Vector3 p = pose.get("pos", Vector3());
    for (int k = 0; k < 3; ++k) r->info.prev_pos[k] = r->info.pos[k];
    r->info.pos[0] = p.x; r->info.pos[1] = p.y; r->info.pos[2] = p.z;
    r->info.yaw = float(double(pose.get("yaw", 0.0)));
    r->info.character = int(pose.get("character", 0));
    const Vector2 f = pose.get("stick", Vector2());
    r->info.facing[0] = f.x; r->info.facing[1] = 0.0f; r->info.facing[2] = f.y; r->info.facing[3] = 1.0f;
    r->info.attack_idle = int(pose.get("idle", 0xe10));
}

void DcWalker::coop_set_remote_status(int id, const PackedByteArray &status) {
    if (!world_ || status.size() != int64_t(dccore::UserStatus::kSize)) return;
    if (dccore::RemotePlayer *r = world_->remote(id)) std::memcpy(r->status.raw.data(), status.ptr(), dccore::UserStatus::kSize);
}

void DcWalker::coop_import_spheres(int source, int remote_id, const PackedFloat32Array &spheres) {
    if (!world_) return;
    auto &map = imported_[source];
    const int n = int(spheres.size() / dccore::kColPackFloats);
    for (int i = 0; i < n; ++i) {
        const float *f = spheres.ptr() + i * dccore::kColPackFloats;
        dccore::ColEntry e;
        dccore::ColPool::unpack(f, e);
        e.remote = remote_id;
        ++coop_spheres_in_;
        auto it = map.find(e.serial);
        if (it == map.end()) {
            Imported im;
            im.index = world_->pool.inject(e);
            im.last_seen = tick_count_;
            map[e.serial] = im;
        } else {
            it->second.last_seen = tick_count_;
            if (it->second.index < 0) continue;
            dccore::ColEntry &c = world_->pool.e[size_t(it->second.index)];
            if (!c.used || c.serial != e.serial || c.remote != remote_id) continue;   // (it was used up here: not again)
            for (int k = 0; k < 3; ++k) { c.pos[k] = e.pos[k]; c.dir[k] = e.dir[k]; c.kick_pos[k] = e.kick_pos[k]; }
            c.radius = e.radius;
            c.kick_speed = e.kick_speed;
            c.kick_decay = e.kick_decay;
        }
    }
    for (auto it = map.begin(); it != map.end();) {   // (a serial is forgotten only after a while: a lost message must not make the same sphere hit twice)
        if (tick_count_ - it->second.last_seen > 180) it = map.erase(it);
        else ++it;
    }
}

void DcWalker::coop_capture(int target_mask) {
    for (const dccore::ColEntry &e : world_->pool.e) {
        if (!e.used || (e.target & target_mask) == 0 || e.remote != 0) continue;
        if (target_mask == 2 && (e.owner_kind < 0 || e.owner_kind > 5)) continue;   // (the player's own spheres, not a monster's that hurt both)
        std::array<float, dccore::kColPackFloats> f;
        dccore::ColPool::pack(e, f.data());
        coop_capture_[e.serial] = f;
    }
}

PackedFloat32Array DcWalker::coop_export_spheres() {
    PackedFloat32Array out;
    for (const auto &kv : coop_capture_) {
        for (float v : kv.second) out.push_back(v);
        ++coop_spheres_out_;
    }
    coop_capture_.clear();
    return out;
}

Array DcWalker::coop_take_patches() {
    Array out;
    if (!world_) return out;
    for (const dccore::StatusPatch &p : world_->status_patches) {
        Dictionary d;
        d["remote"] = p.remote;
        d["offset"] = int(p.offset);
        PackedByteArray b;
        b.resize(int64_t(p.bytes.size()));
        std::memcpy(b.ptrw(), p.bytes.data(), p.bytes.size());
        d["bytes"] = b;
        out.push_back(d);
    }
    world_->status_patches.clear();
    return out;
}

// the monsters (slot, state, hp, position, turn, motion, motion time) and the drops, one record each
PackedFloat32Array DcWalker::coop_snapshot() const {
    PackedFloat32Array out;
    if (!world_) return out;
    out.push_back(float(dccore::MonsterUnit::kSlots));
    for (const dccore::MonsterActor &a : world_->actors) {
        out.push_back(float(a.state)); out.push_back(float(a.active)); out.push_back(float(a.hp));
        out.push_back(a.pos[0]); out.push_back(a.pos[1]); out.push_back(a.pos[2]); out.push_back(a.rot[1]);
        out.push_back(float(a.motion.cur)); out.push_back(a.motion.time);
        out.push_back(float(a.lockon_sw)); out.push_back(a.lockon_dist);   // (set by the monster's script, which only the host runs: the lock-on of the others needs them)
    }
    out.push_back(float(dccore::Drops::kSize));
    out.push_back(world_->drops.spin);
    for (const dccore::Drop &d : world_->drops.d) {
        out.push_back(d.used ? 1.0f : 0.0f); out.push_back(d.pos[0]); out.push_back(d.pos[1]); out.push_back(d.pos[2]);
        out.push_back(float(d.gold)); out.push_back(float(d.item)); out.push_back(float(d.state)); out.push_back(d.timer);
    }
    // the shot effects (the monsters' projectiles and zones; the clients load the same effect models when they place the monsters)
    out.push_back(float(dccore::ShotSystem::kSlots));
    for (const dccore::ShotEffect &fx : world_->shots.slot)
        for (const dccore::ShotEffect::Instance &s : fx.inst) {
            out.push_back(s.active ? 1.0f : 0.0f);
            out.push_back(s.pos[0]); out.push_back(s.pos[1]); out.push_back(s.pos[2]);
            out.push_back(s.vel[0]); out.push_back(s.vel[1]); out.push_back(s.vel[2]);
            out.push_back(float(s.ms.cur)); out.push_back(s.ms.time);
        }
    return out;
}

void DcWalker::coop_apply_snapshot(const PackedFloat32Array &snap) {
    if (!world_ || snap.size() < 2) return;
    const float *f = snap.ptr();
    int at = 0;
    const int slots = int(f[at++]);
    for (int i = 0; i < slots; ++i) {
        if (at + 11 > snap.size()) return;
        if (i < dccore::MonsterUnit::kSlots) {
            dccore::MonsterActor &a = world_->actors[size_t(i)];
            a.state = int(f[at]); a.active = int(f[at + 1]); a.hp = int(f[at + 2]);
            a.pos[0] = f[at + 3]; a.pos[1] = f[at + 4]; a.pos[2] = f[at + 5]; a.rot[1] = f[at + 6];
            a.motion.cur = int(f[at + 7]); a.motion.time = f[at + 8];
            a.lockon_sw = int(f[at + 9]); a.lockon_dist = f[at + 10];
        }
        at += 11;
    }
    if (at + 2 > snap.size()) return;
    const int drops = int(f[at++]);
    world_->drops.spin = f[at++];
    for (int i = 0; i < drops; ++i) {
        if (at + 8 > snap.size()) return;
        if (i < dccore::Drops::kSize) {
            dccore::Drop &d = world_->drops.d[size_t(i)];
            d.used = f[at] != 0.0f;
            d.pos[0] = f[at + 1]; d.pos[1] = f[at + 2]; d.pos[2] = f[at + 3]; d.pos[3] = 1.0f;
            d.gold = int(f[at + 4]); d.item = int(f[at + 5]); d.state = int(f[at + 6]); d.timer = f[at + 7];
        }
        at += 8;
    }
    if (at >= snap.size()) return;
    const int slots_n = int(f[at++]);
    for (int si = 0; si < slots_n; ++si)
        for (int ii = 0; ii < dccore::ShotEffect::kInstances; ++ii) {
            if (at + 9 > snap.size()) return;
            if (si < dccore::ShotSystem::kSlots && si != world_->player_fx) {   // (my own effect slot is mine)
                dccore::ShotEffect::Instance &s = world_->shots.slot[size_t(si)].inst[size_t(ii)];
                s.active = f[at] != 0.0f;
                s.pos[0] = f[at + 1]; s.pos[1] = f[at + 2]; s.pos[2] = f[at + 3];
                s.vel[0] = f[at + 4]; s.vel[1] = f[at + 5]; s.vel[2] = f[at + 6];
                s.ms.cur = int(f[at + 7]); s.ms.time = f[at + 8];
            }
            at += 9;
        }
}

void DcWalker::coop_apply_patch(int offset, const PackedByteArray &bytes) {
    if (!world_ || offset < 0 || int64_t(offset) + bytes.size() > int64_t(dccore::UserStatus::kSize)) return;
    std::memcpy(world_->status.raw.data() + offset, bytes.ptr(), size_t(bytes.size()));
}

PackedByteArray DcWalker::coop_status_bytes() const {
    PackedByteArray out;
    if (!world_) return out;
    out.resize(int64_t(dccore::UserStatus::kSize));
    std::memcpy(out.ptrw(), world_->status.raw.data(), dccore::UserStatus::kSize);
    return out;
}

void DcWalker::coop_show_hits(const Array &hits) {
    for (int i = 0; i < hits.size(); ++i) hit_values_.push_back(hits[i]);
}

// The state of the floor's events as ints: chests (state, active) x 0x18, statues (active, state) x 8, traps (state) x 3, trigger areas (on), doors (the layout's
// door_open of every cell) and the event slots (type, state) x 0x30.
PackedInt32Array DcWalker::coop_event_state() const {
    PackedInt32Array out;
    if (!events_) return out;
    const dccore::FloorEvents &ev = events_->ev;
    for (const dccore::Chest &c : ev.chests) { out.push_back(c.state); out.push_back(c.active ? 1 : 0); out.push_back(c.quality); }
    for (const dccore::AtraBall &a : ev.atras) { out.push_back(a.active ? 1 : 0); out.push_back(a.state); }
    for (const dccore::TrapCircle &t : ev.traps) out.push_back(t.state);
    for (const dccore::MarkerState &m : events_->marker_state) out.push_back(m.on ? 1 : 0);
    for (int i = 0; i < dccore::kGrid * dccore::kGrid; ++i) out.push_back(size_t(i) < layout_.door_open.size() ? layout_.door_open[size_t(i)] : 0);
    for (const dccore::FloorEvent &e : ev.events) { out.push_back(e.type); out.push_back(e.state); }
    for (int v : layout_.dran_state) out.push_back(v);   // Dran's arena tiles (they only ever go down)
    return out;
}

void DcWalker::coop_apply_event_state(const PackedInt32Array &pairs) {
    if (!events_) return;
    dccore::FloorEvents &ev = events_->ev;
    const int nch = int(ev.chests.size()) * 3, nat = int(ev.atras.size()) * 2, ntr = int(ev.traps.size());
    const int nm = int(events_->marker_state.size()), nd = dccore::kGrid * dccore::kGrid;
    for (int k = 0; k + 1 < pairs.size(); k += 2) {
        int i = pairs[k];
        const int v = pairs[k + 1];
        if (i < 0) continue;
        if (i < nch) {
            dccore::Chest &c = ev.chests[size_t(i / 3)];
            if (i % 3 == 0) c.state = v; else if (i % 3 == 1) c.active = v != 0; else c.quality = v;
            continue;
        }
        i -= nch;
        if (i < nat) {
            dccore::AtraBall &a = ev.atras[size_t(i / 2)];
            if (i % 2 == 0) a.active = v != 0; else a.state = v;
            continue;
        }
        i -= nat;
        if (i < ntr) { ev.traps[size_t(i)].state = v; continue; }
        i -= ntr;
        if (i < nm) { events_->marker_state[size_t(i)].on = v != 0; continue; }
        i -= nm;
        if (i < nd) {
            if (size_t(i) < layout_.door_open.size()) {
                const int old = layout_.door_open[size_t(i)];
                layout_.door_open[size_t(i)] = uint8_t(v);
                if (floor_) {
                    if ((v & 1) && !(old & 1)) floor_->open_door(i % dccore::kGrid, i / dccore::kGrid, 1);
                    if ((v & 2) && !(old & 2)) floor_->open_door(i % dccore::kGrid, i / dccore::kGrid, 2);
                }
            }
            continue;
        }
        i -= nd;
        if (i < int(ev.events.size()) * 2) {
            dccore::FloorEvent &e = ev.events[size_t(i / 2)];
            if (i % 2 == 0) e.type = v; else e.state = v;
            continue;
        }
        i -= int(ev.events.size()) * 2;
        if (i >= 0 && i < 12 && v < layout_.dran_state[size_t(i)]) {   // a tile the host's shots took down: the crumbling starts here too
            layout_.dran_state[size_t(i)] = v;
            if (v == 1) layout_.dran_time[size_t(i)] = 0.0f;
        }
    }
}

void DcWalker::coop_mimic(int slot) {
    if (world_ && slot >= 0 && slot < dccore::MonsterUnit::kSlots) world_->actors[size_t(slot)].active = 1;
}

int DcWalker::drop_bag_item(int slot) {
    if (!world_ || slot < 0 || slot >= dccore::kBagSlots) return -1;
    const int id = world_->status.get<int16_t>(0x436e + size_t(slot) * 2);
    if (id < 0x84 || id > 0x100) return -1;
    dccore::consume_bag_slot(world_->status, slot);
    return id;
}

void DcWalker::coop_spawn_drop(const Vector3 &pos, int item) {
    if (!world_) return;
    const float p[3] = {pos.x, pos.y, pos.z};
    world_->drops.set(p, -1, item);
}

bool DcWalker::ev_motion_range(int slot, int motion, float &start, float &end, float &speed) {
    DcCharacter *n = nullptr;
    auto it = ev_actor_nodes_.find(slot);
    if (it != ev_actor_nodes_.end()) n = it->second;
    else if (slot == -1) n = player_;
    if (!n) return false;
    const Dictionary d = n->get_motion_info(motion);
    if (!d.has("end")) return false;
    start = float(double(d["start"]));
    end = float(double(d["end"]));
    speed = d.has("speed") && double(d["speed"]) > 0.0 ? float(double(d["speed"])) : 1.0f;
    return true;
}

Dictionary DcWalker::get_eb_ui() const { return eb_ui_dictionary(session_ ? session_->eb_state() : nullptr); }
