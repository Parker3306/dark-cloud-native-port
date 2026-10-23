#pragma once
#include <memory>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <dccore/dungeon.h>
#include <dccore/dungeon_session.h>
#include <dccore/pack.h>
#include <functional>
#include <map>
#include <dccore/event_world.h>
#include <dccore/status_ops.h>
#include <dccore/items.h>
#include <dccore/floor.h>
#include <dccore/monster_ai.h>
#include <dccore/monsters.h>
#include <dccore/player.h>
#include <dccore/tables.h>
#include <dccore/walk.h>
#include "dc_character.h"
#include "dc_dungeon.h"

namespace godot {

// Playable dungeon floor: the floor, the player character and the game's own follow camera. Steps the dccore walker 60 times per second from the keyboard or
// a gamepad (or from a scripted pad sequence set with set_script, for automated tests and screenshots).
class DcWalker : public Node3D {
    GDCLASS(DcWalker, Node3D)

    std::shared_ptr<dccore::Dungeon> dungeon_;
    dccore::FloorLayout layout_;
    std::unique_ptr<dccore::Walker> walker_;
    DcDungeon *floor_ = nullptr;
    DcCharacter *player_ = nullptr;
    Camera3D *camera_ = nullptr;
    std::shared_ptr<dccore::MonsterUnit> monsters_;
    std::unique_ptr<dccore::MonsterWorld> world_;   // the monsters' behaviour (scripts, movement, motion)
    dccore::GameRand rng_;
    std::vector<DcCharacter *> monster_nodes_;   // one per monster slot (null when empty)
    double accumulator_ = 0.0;
    int tick_count_ = 0;
    int last_motion_ = -1;
    std::vector<dccore::PadRaw> script_;   // when not empty: pad input for the first ticks instead of the real devices
    bool use_devices_ = true;
    bool fighting_ = false;   // the walker runs with a player record, the hit-box pool and a motion table (combat)

    dccore::PadRaw read_devices() const;
    void apply_state();
    void apply_monsters();
    void apply_drops();
    void apply_events();
    std::unique_ptr<dccore::UserStatus> initial_status_;
    std::unique_ptr<dccore::EventWorld> events_;
    std::shared_ptr<dccore::EventTables> event_tables_;
    std::vector<Node3D *> chest_nodes_, atra_nodes_, marker_nodes_, trap_nodes_;
    std::vector<Node3D *> shot_nodes_;
    std::vector<DcCharacter *> shot_chars_;   // the effect models of the shot effects (slot * 8 + instance)
    void apply_shots();
    // the dungeon's story script (dun/script/d0N/event.stb): the events of story floors
    std::shared_ptr<dccore::DungeonSession> session_;
    uint32_t session_prev_buttons_ = 0;
    bool event_hold_ = false;
    int floor_start_pending_ = -1;   // the floor-start script is run on the first tick
    std::unique_ptr<dccore::EventHost> ev_host_;
    std::vector<std::string> ev_pack_paths_;
    std::map<std::string, std::shared_ptr<dccore::Pack>> ev_packs_;
    std::map<int, DcCharacter *> ev_actor_nodes_;
    std::map<int, uint32_t> ev_actor_serial_;
    struct StageClock { double time = 0.0, start = 0.0, end = 0.0, speed = 1.0; bool loop = true; };
    std::map<int, StageClock> ev_clock_;    // the motion clock of every stage actor, advanced by the game ticks (not by the frames)
    void step_stage_actor(int slot, DcCharacter *node, const dccore::EvActor &a);
    bool stage_was_running_ = false;
    std::vector<bool> script_inside_;   // per marker: the player stood in its area last tick (EventWorld keeps its own flag for the markers it handles)
    void apply_object_anims();
    void apply_event_stage();       // the cut-scene's actors and camera while a story event runs
    void clear_event_stage();
    void poll_script_markers(const dccore::PadRaw &pad, bool ok_down);
    void poll_hit_markers();            // the character rooms: a sphere of the right character reaches a hit marker (Goro's log, Ruby's crystals, Ungaga's blast)
    std::vector<int> hint_char_;        // per marker: the character who already read the hint there (his next OK press is an attack, not a question)
    int ruby_key_ = 0;                  // the element the Ruby crystals of this floor want
    std::array<int, 12> dran_shown_ = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};   // what the arena tile nodes show (Dran's floor)
    std::map<std::string, Vector3> frame_moves_;   // frames a room script moved (doors, switches): where they are now
    std::map<std::string, bool> frame_shown_;      // frames a room script showed or hid
    int spawn_script_monster(int k, bool load, const float *pos, int death_event);
    // ---- the back floor ("ura", notes/back_floors.md): the floor's second layout, swapped in place when a room script asks (_SET_URA_DUNGEON).
    // Everything that belongs to one of the two floors lives in a FloorSlot; the one not in play sits on the shelf.
    struct FloorSlot {
        dccore::FloorLayout layout;
        std::unique_ptr<dccore::EventWorld> events;
        DcDungeon *node = nullptr;
        std::vector<Node3D *> chests, atras, markers, traps;
        std::array<uint8_t, 400> visited{};
        std::array<uint8_t, 16> room_seen{};
        std::vector<bool> inside;
        std::vector<int> hint;
    };
    struct BackFloor {
        bool present = false;
        dccore::FloorEvents events;
        std::vector<dccore::FloorMarker> markers;
        std::function<DcDungeon *(const dccore::FloorLayout &)> make_node;
        std::function<void(dccore::MonsterUnit &, bool)> load_monsters;
    } back_;
    FloorSlot shelf_;
    dccore::FloorEvents main_initial_events_;   // the main floor's chests as they were built (co-op: the monsters of both floors are arranged from these so that every player gets the same ones)
    bool in_back_ = false, ura_pending_ = false;
    int coop_players_ = 1;
    const dccore::GameTables *game_tables_ = nullptr;
    int ev_map_ = 0, ev_floor_ = 0;
    void swap_floor();
    bool auto_item_pick_ = false;        // tests: the item window answers at once with the first listed item the bag holds
    int ibox_ = 0;                       // DAT_01d567ec: the chest the script is asking about
    void start_trap_script(int chest, int trap);   // a trapped chest was pressed: function 15 (16 for the Happy Clown)
    void build_monster_nodes();
    void attach_mimics(bool reset);
    void sync_chest_nodes();
    void make_chest_node(const dccore::Chest &c);
    void apply_throw();   // the reticle, the items in flight and the flashes of the ones that went off
    struct BlastNode { MeshInstance3D *node; int age; float size; };
    std::vector<BlastNode> blast_nodes_;
    MeshInstance3D *reticle_node_ = nullptr;
    std::vector<MeshInstance3D *> thrown_nodes_;
    int shown_weapon_ = -1;   // the weapon id the weapon node shows
    std::function<std::shared_ptr<dccore::PlayerKit>(int character, int weapon_id, int element)> weapon_changer_;
    std::function<void(dccore::UserStatus &, int)> atra_handler_;
    std::function<void(int, int)> boss_handler_;
    bool boss_floor_ = false, boss_done_ = false;
    int boss_timer_ = 0, boss_map_ = 0, boss_floor_no_ = 0;
    bool prev_ok_ = false;
    bool exit_requested_ = false;
    Array notice_queue_;
    Array hit_values_;                   // floating damage numbers: [{pos, value, type}] (type 0 monster hit, 2 player hit, -1 no effect)
    int last_life_ = -1, last_life_char_ = -1;
    std::array<uint8_t, 400> visited_{};      // the mini map's explored cells (map + 0x8710) and rooms (map + 8)
    std::array<uint8_t, 16> room_seen_{};
    void update_visited();
    std::vector<Node3D *> drop_nodes_;   // one marker per drop slot
    std::shared_ptr<dccore::ItemDb> item_db_;
    void enable_combat();
    DcCharacter *weapon_node_ = nullptr;   // the sword model on the player's hand
    std::shared_ptr<dccore::PlayerKit> kit_;   // the character in play: model, weapon, hit points of the weapon, motion table, main effect

protected:
    static void _bind_methods();

public:
    void setup(std::shared_ptr<dccore::Dungeon> dungeon, const dccore::FloorLayout &layout, DcCharacter *player, DcDungeon *floor);
    // Puts the floor's monsters on the floor (ArrangementPos with the given rand() state, running each monster's init script) and starts their behaviour.
    // Their models are instanced per slot and follow the simulation.
    void set_weapon_node(DcCharacter *weapon) { weapon_node_ = weapon; if (player_ && weapon) player_->add_child(weapon); }
    void set_item_db(std::shared_ptr<dccore::ItemDb> db) { item_db_ = std::move(db); }
    // The floor's chests, atra statues and trigger areas (see dccore/event_world.h); the walker lets the player use them and reports what happened as notices.
    void set_events(dccore::FloorEvents events, std::vector<dccore::FloorMarker> markers, std::shared_ptr<dccore::EventTables> tables, int map_no, int floor, int gate_key);
    Array take_notices();
    Dictionary get_minimap() const;       // {img, tiles[400] (-1 none), visited[400], player: Vector2 (world x, z), angle, monsters: [Vector2], items: [Vector2]}
    Array take_hit_values() { Array a = hit_values_; hit_values_ = Array(); return a; }                 // [{kind, a, b}] since the last call
    bool is_exit_requested() const { return exit_requested_; }
    void clear_exit_request() { exit_requested_ = false; }
    Array get_bag() const;                  // [{slot, id, value}] of the occupied bag slots
    Dictionary use_bag_item(int slot);
    int drop_bag_item(int slot);                       // co-op gift: takes one of the bag slot out of the record and returns its item id (-1: nothing there)
    void coop_spawn_drop(const Vector3 &pos, int item);   // host: an item lies on the floor for everybody (the drop pool is the host's)
    bool assign_quick(int quick_slot, int bag_slot);   // moves every bag entry of that item into the quick-use slot (0..2); what was in the slot goes back to the bag      // {used, life, water, poisoned}: applies the item (usedActiveItem) and takes it out of the bag
    Array get_weapons() const;              // [{slot, id, level, attack, durability, max_durability, exp, current}] of the character in play
    bool equip_weapon(int slot);            // makes weapon `slot` the one in hand
    Dictionary get_event_info() const;    // {chests: [{pos, item, small, opened}], atras: [{pos, id, active}], markers: [{name, event, pos, radius, on}]} (for tools and debugging)
    dccore::UserStatus *status_ptr() { return world_ ? &world_->status : nullptr; }
    dccore::GameRand &game_rand() { return rng_; }
    void set_initial_status(const dccore::UserStatus &s) { initial_status_ = std::make_unique<dccore::UserStatus>(s); }   // the player record to play with (before set_monsters)
    dccore::EventWorld *event_world() { return events_.get(); }
    void set_kit(std::shared_ptr<dccore::PlayerKit> kit) { kit_ = std::move(kit); }
    // Another character takes over in the middle of the floor (the party menu: selectChrUnit): new model and weapon nodes, motion table, weapon points, main effect.
    bool swap_character(std::shared_ptr<dccore::PlayerKit> kit, DcCharacter *new_player, DcCharacter *new_weapon);
    int get_character() const { return world_ ? world_->status.character() : 0; }
    void debug_set_monster_hp(int slot, int hp) { if (world_ && slot >= 0 && slot < dccore::MonsterUnit::kSlots) world_->actors[size_t(slot)].hp = hp; }   // tests
    void debug_boss_down() { if (world_) world_->run_script_request = 510; }   // tests: as if the boss's death script ran
    void debug_trace(bool on) { if (world_) { world_->trace = on; world_->trace_ext = on; } }   // tests
    PackedStringArray debug_log() const { PackedStringArray a; if (world_) { for (const auto &l : world_->ext_log) a.push_back(String(l.c_str())); for (const auto &l : world_->log) a.push_back(String(l.c_str())); } return a; }
    std::function<std::shared_ptr<dccore::Pack>(const std::string &)> pack_loader;   // reads a pack file of the disc (for the cut-scenes' models)
    dccore::EventHost *event_host();                                                  // the host the dungeon's script runtime talks to (actors of the stage)
    bool ev_load_pack(int index, const std::string &path);
    bool ev_load_actor(int slot, int pack, const std::string &entry);
    void ev_unload_actor(int slot);
    bool ev_motion_info(int slot, int motion, float &start, float &end);
    bool ev_motion_range(int slot, int motion, float &start, float &end, float &speed);   // the cfg row of a motion of a stage actor (false: the actor has none)
    Dictionary get_eb_ui() const;
    void debug_run_function(int id) { if (session_) { const float p[3] = {0, 0, 0}; session_->start(id, 0, p, false); } }   // tests: start a story function of this floor
    PackedStringArray get_eb_log() const { PackedStringArray a; if (session_ && session_->eb_state()) for (const auto &l : session_->eb_state()->log) a.push_back(String(l.c_str())); if (session_) { String u = "unknown ext:"; for (int id : session_->rt().unknown_ext_ids) u += " " + String::num_int64(id); a.push_back(u); } return a; }
    int eb_autoplay_ = -1;   // tests: >= 0 plays the EB duel perfectly, pressing each key that many ticks after its line
    void debug_eb_autoplay(int offset) { eb_autoplay_ = offset; }                                                          // the EB duel mini-game: what the overlay draws (empty when none runs)
    float ev_motion_time(int slot);
    // The back floor of this random floor: its layout and events (built right after the main floor's, as BtCleatRandomMap does), the maker of its floor node and the loader of its monsters.
    void set_back_floor(const dccore::FloorLayout &layout, dccore::FloorEvents events, std::vector<dccore::FloorMarker> markers, std::function<DcDungeon *(const dccore::FloorLayout &)> make_node,
            std::function<void(dccore::MonsterUnit &, bool)> load_monsters);
    bool is_back_floor() const { return in_back_; }
    // co-op balance (dccore/coop_scale.h): the players of this floor, the same on every peer; set before set_monsters
    void set_coop_players(int n) { coop_players_ = dccore::coop_clamp_players(n); if (world_) world_->set_coop_players(coop_players_); }
    int get_coop_players() const { return coop_players_; }
    bool has_back_floor() const { return back_.present; }
    void debug_swap_floor() { swap_floor(); }   // tests
    void debug_kill_player() { if (world_) { const int ch = world_->status.character(); world_->status.set_life(ch, 0); world_->status.set<int16_t>(0x8a5c + size_t(ch) * 2, 0); world_->status.set<int16_t>(0x8a68 + size_t(ch) * 2, 0); } }   // tests
    void coop_revive();   // co-op: a fallen player gets up at the floor's entrance with half his life (instead of the game over, which would take the host's floor away from everybody)
    void set_auto_item_pick(bool on) { auto_item_pick_ = on; }
    void open_item_modal(int kind, const std::vector<int> &args);   // the event host: a window asks which item to use (notice 43 for the front end)
    void modal_done(int value) { if (session_) session_->rt().modal_done(value); }   // the front end's answer
    void debug_set_chest_trap(int chest, int id) { if (events_ && chest >= 0 && size_t(chest) < events_->ev.chests.size()) events_->ev.chests[size_t(chest)].quality = id; }   // tests
    void debug_set_event_flag(int i, int v) { if (world_ && i >= 0 && i < 50) world_->status.raw[0x8aa4 + size_t(i)] = uint8_t(v); }   // tests: a per-floor event flag
    void set_ruby_key(int k) { ruby_key_ = k; if (session_) session_->ruby_key = k; }
    void set_dungeon_session(std::shared_ptr<dccore::DungeonSession> s, int floor = -1);   // floor >= 0: run the floor-start script of that story room
    Dictionary get_event_ui() const;   // the story event in progress: {running, windows [{text, state}], fade_alpha, fade_color}
    PackedFloat32Array debug_floor_scan(double x0, double x1, double z0, double z1, double step, double from_y) const;   // tests: floor height under a grid (-9999 = none)
    bool is_view_mode() const { return walker_ && walker_->view_mode; }
    void debug_full_life() { if (world_) { const int ch = world_->status.character(); world_->status.set_life(ch, world_->status.max_life(ch)); world_->status.set<int16_t>(0x8a5c + size_t(ch) * 2, world_->status.max_life(ch)); world_->status.set<int16_t>(0x8a68 + size_t(ch) * 2, 0); { const dccore::WeaponView w = world_->status.stored_weapon(ch); const size_t off = size_t(w.p - world_->status.raw.data()); world_->status.set<float>(off + 0x10, float(w.max_durability())); } if (walker_ && !walker_->combat.dead) walker_->combat.invuln = 120; } }   // tests
    void debug_kill_monsters() { if (world_) for (auto &a : world_->actors) if (a.state == 2) { a.hp = 0; a.state = -1; if (a.death_event != -1) a.death_event_pending = 1; } }   // tests
    void set_boss_floor(bool on) { boss_floor_ = on; }
    void set_start(double x, double z, double yaw, double y = 30.0);   // where the hero begins a floor whose intro scene put him somewhere (boss rooms)   // a boss room: the models of the floor's layout row are the boss and its helpers, created at once
    void set_boss_handler(std::function<void(int, int)> f) { boss_handler_ = std::move(f); }   // (map, floor): the boss died, the fade is over
    float get_boss_fade() const { return boss_timer_ > 0 ? 1.0f - float(boss_timer_) / 120.0f : (boss_done_ ? 1.0f : 0.0f); }
    void set_atra_handler(std::function<void(dccore::UserStatus &, int)> f) { atra_handler_ = std::move(f); }   // an Atla statue was taken: the entry of the dungeon's table
    void set_weapon_changer(std::function<std::shared_ptr<dccore::PlayerKit>(int, int, int)> f) { weapon_changer_ = std::move(f); }
    void refresh_weapon();   // the weapon in the record changed (it broke, or was equipped in the menu): new weapon model, hit points, main effect
    void set_monsters(std::shared_ptr<dccore::MonsterUnit> unit, const dccore::GameTables &tables, int map_no, uint32_t rand_state, dccore::CollisionPiece monster_shape, const dccore::FloorEvents *events = nullptr, int floor = 0, int map1_variant = 0);
    void _physics_process(double delta) override;

    // Scripted input: one entry per tick, [lx, ly, rx, ry, buttons] raw pad bytes.
    void set_script(const Array &ticks);
    void set_use_devices(bool on) { use_devices_ = on; }
    void set_hold_pad(int lx, int ly, int buttons) { hold_pad_.lx = uint8_t(lx); hold_pad_.ly = uint8_t(ly); hold_pad_.buttons = uint32_t(buttons); hold_pad_on_ = true; }   // tests: the pad every tick after the script (a bot steers with it)
    dccore::PadRaw hold_pad_;
    bool hold_pad_on_ = false;
    int get_tick_count() const { return tick_count_; }
    Vector3 get_player_position() const;
    Dictionary get_camera_state() const;
    Dictionary get_hero_pose() const;
    // ---- co-op (see notes/coop_native.md). Role 1 = host: the monsters are simulated here for every player; role 2 = client: they are the host's, shown from snapshots.
    void coop_set_role(int role);
    int coop_add_remote(const PackedByteArray &status);                       // host: another player's record -> his id
    void coop_remove_remote(int id);
    void coop_update_remote(int id, const Dictionary &pose);                  // host: where he is (from his presence)
    void coop_set_remote_status(int id, const PackedByteArray &status);       // host: a fresh copy of his record
    void coop_import_spheres(int source, int remote_id, const PackedFloat32Array &spheres);   // host: his attack spheres; client: the monsters' (remote_id 0)
    PackedFloat32Array coop_export_spheres();
    PackedFloat32Array coop_export_fx() const;                                // my own weapon effect (instances and bullets) for the others to draw
    void coop_apply_fx(int peer, const PackedFloat32Array &d);               // another player's
    void coop_clear_fx(int peer);                                // the spheres seen since the last call (a host: the monsters', a client: the player's own), as floats
    Array coop_take_patches();                                                // host: [{remote, offset, bytes}] changes of the remote records
    PackedFloat32Array coop_snapshot() const;                                 // host: the monsters and the drops
    void coop_apply_snapshot(const PackedFloat32Array &snap);                 // client
    void coop_apply_patch(int offset, const PackedByteArray &bytes);          // client: the host changed the record (weapon exp, gold, items)
    PackedByteArray coop_status_bytes() const;                                // the player record (the host keeps a copy of every client's)
    Array coop_take_hit_values() { Array a = coop_hit_out_; coop_hit_out_ = Array(); return a; }     // host: the floating numbers to show on the clients as well
    Array coop_take_remote_notices() { Array a = coop_notice_out_; coop_notice_out_ = Array(); return a; }   // host: [{remote, kind, a, b}] weapon messages of the clients
    PackedInt32Array coop_event_state() const;                                // the floor's chests, statues, traps, trigger areas, doors as one list of ints (to compare and send changes)
    void coop_apply_event_state(const PackedInt32Array &pairs);               // [index, value, ...] changes another player made
    void coop_mimic(int slot);                                                // host: a client opened a mimic chest
    void coop_show_hits(const Array &hits);
    Array coop_take_sounds() { Array a = coop_sound_out_; coop_sound_out_ = Array(); return a; }   // host: the monsters' sounds, for the clients
    void coop_play_sounds(const Array &sounds) { for (int i = 0; i < sounds.size(); ++i) notice_queue_.push_back(sounds[i]); }   // client
    void coop_show_notice(const Dictionary &n) { notice_queue_.push_back(n); }   // client: a message of my weapon that the host noticed                                   // client: floating numbers seen by the host
    void set_player_color(int index) { color_index_ = index; if (player_) player_->set_player_color(index); }   // co-op: the clothes of this player (also after a character change)
    int color_index_ = -1;
    bool input_blocked_ = false;
 public:
    void set_input_blocked(bool b) { input_blocked_ = b; }   // co-op: while a player's menu is open the others keep playing; his character just stands there
 private:
    int coop_role_ = 0;
    int coop_spheres_in_ = 0, coop_remote_hits_ = 0, coop_spheres_out_ = 0;
 public:
    Dictionary coop_debug() const { Dictionary d; d["spheres_in"] = coop_spheres_in_; d["spheres_out"] = coop_spheres_out_; d["remote_hits"] = coop_remote_hits_; d["remotes"] = world_ ? int(world_->remotes.size()) : 0; return d; }
 private:
    struct Imported { int index = -1; int last_seen = 0; };
    std::map<int, std::map<uint32_t, Imported>> imported_;   // per source: serial of a sphere that came over the wire -> its entry in the pool (-1: dropped) and when it was last listed
    std::map<uint32_t, std::array<float, dccore::kColPackFloats>> coop_capture_;   // the spheres that were in the pool at the points of a tick where they can be seen (they live for one tick)
    void coop_capture(int target_mask);
    // the other players' weapon effects (Goro's wave, Ruby's orbs, Xiao's bullets, Osmond's gun ...): what each peer sends of its own effect slot, drawn here (they are not part of this world)
    struct RemoteFx {
        int character = -1, weapon = -1, element = -1;
        std::shared_ptr<dccore::Character> model;
        bool look_at = false;
        std::vector<std::array<float, 9>> inst;   // pos3, vel3, motion, time, spare
        std::vector<std::array<float, 5>> pts;    // x, y, z, radius, kind (bullets, gun, flame)
        std::vector<DcCharacter *> chars;
        std::vector<MeshInstance3D *> dots;
        int age = 0;
    };
    std::map<int, RemoteFx> remote_fx_;
    void apply_remote_fx();
    Array coop_hit_out_, coop_notice_out_, coop_sound_out_;   // co-op: {pos, yaw, motion, time, visible} of the player as the other players see him
    Camera3D *get_camera() const { return camera_; }
    DcDungeon *get_floor() const { return floor_; }
    Dictionary get_player_status() const;   // {life, max_life, mode, invuln, dead, game_over}
    Array get_monster_info() const;
    Array debug_monster_cols() const;
    Array debug_player_spheres() const;
    Array debug_polys_near(double x, double z) const;
    bool dbg_ev_was_running_ = false; Vector3 dbg_ev_end_pos_; float dbg_ev_end_yaw_ = 0; int dbg_ev_end_tick_ = -1, dbg_ev_end_count_ = 0;   // tests: where the hero stood when the last script event ended
    void debug_dran_hit(int tile) { if (tile >= 0 && tile < 12 && layout_.dran_state[size_t(tile)] > 1) --layout_.dran_state[size_t(tile)]; }   // tests: a shot hit that tile
    PackedInt32Array get_dran_state() const { PackedInt32Array a; for (int v : layout_.dran_state) a.push_back(v); return a; }
    Dictionary debug_event_end() const { Dictionary d; d["pos"] = dbg_ev_end_pos_; d["yaw"] = dbg_ev_end_yaw_; d["tick"] = dbg_ev_end_tick_; d["count"] = dbg_ev_end_count_; return d; }   // tests: the collision polygons of the cell around (x, z): [{v0, v1, v2, n, attr}]
    float dbg_near_[4] = {1e9f, 0, 0, 0};   int dbg_near_slot_ = -1, dbg_near_tick_ = -1;   // tests: the closest a window-open body sphere came to the player (distance, its position)
    float dbg_best_gap_ = 1e9f; int dbg_best_slot_ = -1, dbg_best_tick_ = -1, dbg_sphere_ticks_ = 0;   // tests: the smallest gap (distance minus radii) between a player sphere and a monster body sphere that was on
    Dictionary debug_hit_stats() const { Dictionary d; d["gap"] = dbg_best_gap_; d["slot"] = dbg_best_slot_; d["tick"] = dbg_best_tick_; d["sphere_ticks"] = dbg_sphere_ticks_; d["near"] = dbg_near_[0]; d["near_pos"] = Vector3(dbg_near_[1], dbg_near_[2], dbg_near_[3]); d["near_slot"] = dbg_near_slot_; d["near_tick"] = dbg_near_tick_; return d; }     // tests: the player's attack spheres in the hit pool [{pos, r}]       // tests: per monster the body spheres that are on (bone, radius, world position, window), muteki and col_off       // [{slot, name, pos}] of the monsters on the floor
    void teleport(double x, double z, double y = 30.0);   // puts the player (and the camera behind them) at a world position
    void step_ticks(int n);                // advances n ticks immediately using the script / devices
    void override_view(const Vector3 &player_pos, double yaw, const Vector3 &camera_pos, const Vector3 &camera_ref);   // static placement (for comparing with game screenshots)
};

} // namespace godot
