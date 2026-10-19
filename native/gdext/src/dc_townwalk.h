#pragma once
#include <map>
#include <memory>
#include <vector>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <dccore/disc.h>
#include <dccore/edit.h>
#include <dccore/event_runtime.h>
#include <dccore/fishing.h>
#include <dccore/georama.h>
#include <dccore/items.h>
#include <dccore/pack.h>
#include <dccore/savedata.h>
#include <dccore/text.h>
#include <dccore/town.h>
#include <dccore/villager_walk.h>
#include <dccore/user_status.h>
#include <dccore/walk.h>
#include "dc_character.h"
#include "dc_town.h"

namespace godot {

// What the town needs from the running game (owned by DcModels).
struct TownEnv {
    dccore::Disc *disc = nullptr;
    dccore::SaveData *save = nullptr;
    dccore::UserStatus *status = nullptr;
    const dccore::ItemDb *items = nullptr;
    dccore::GameRand *rng = nullptr;
    std::shared_ptr<dccore::MessageBank> system_text;
    const dccore::FishTables *fish_tables = nullptr;     // the fish and the baits (fishing)
    const dccore::GeoTables *geo_tables = nullptr;      // the Georama tables of the main executable (null: no editing)
    std::shared_ptr<dccore::MessageBank> atla_text;     // commenu/a_usa/emenu.pak: atrames.bin (names of parts, tips and villagers)
    int map_index = 0;
    int old_map = 0;
};

class TownHost;

// Walking through a town and its events: the game's own player movement and follow camera on the town's collision, the trigger points (doors, items, events),
// the villagers and their talk, and the event scripts (cutscenes) of the map.
class DcTownWalk : public Node3D {
    GDCLASS(DcTownWalk, Node3D)
    friend class TownHost;

    enum Mode { ModeWalk, ModeEvent, ModeTalk, ModeEdit, ModeShop, ModeFish };

    std::shared_ptr<dccore::Town> town_;
    TownEnv env_;
    std::unique_ptr<dccore::Walker> walker_;
    DcTown *scene_ = nullptr;
    DcCharacter *player_ = nullptr;
    Camera3D *camera_ = nullptr;
    std::unique_ptr<dccore::EventRuntime> rt_;
    std::unique_ptr<TownHost> host_;
    std::map<int, DcCharacter *> actor_nodes_;       // event actors by slot (not the hero)
    std::map<int, uint32_t> actor_serial_, actor_generation_;
    std::vector<DcCharacter *> villager_nodes_;      // standing villagers of the town: per TownPerson
    std::vector<bool> villager_visible_;
    std::vector<dccore::Roamer> roam_;               // per TownPerson: who walks around and where (notes/villagers.md section 4)
    std::vector<int> roam_motion_;
    std::array<std::vector<int>, 4> phase_rows_;      // EdCreateVillagerTable: who is outdoors in each part of the day (PEOPLE_LIST shuffled at the map entry; empty: TIME_TABLE)
    void init_roamers();                             // EdInitVilagerPosition: half of the present villagers stand, the rest start on a free cell near their house
    void step_roamers();                             // EdMoveVillager: the random walk
    std::map<std::string, std::shared_ptr<dccore::Pack>> packs_;
    std::vector<std::string> event_pack_paths_;
    Mode mode_ = ModeWalk;
    double accumulator_ = 0.0;
    int tick_count_ = 0, last_motion_ = -1;
    bool prev_ok_ = false;
    uint32_t prev_menu_ = 0;
    std::vector<dccore::PadRaw> script_;
    bool use_devices_ = true;
    Array pending_nav_;
    int near_event_ = -1, near_person_ = -1;
    bool free_cam_ = false;
    Vector3 free_pos_, free_ref_;
    // talk
    int talk_person_ = -1, talk_select_ = 0, talk_phase_ = 0;
    // villagers that were told to appear / disappear by the scripts
    std::map<int, int> villager_override_;
    int bgm_request_ = -1;
    Array sound_events_;
    dccore::InteriorCamera interior_camera_;
    std::vector<std::pair<std::string, int>> sound_log_;
    bool event_pending_start_ = false;
    struct HandleApplied { bool drawn = true; bool moved = false; dccore::EvVec3 pos, rot; };
    HandleApplied handle_applied_[32];
    std::vector<int> fixed_instances_, placed_instances_;   // script object indices -> instance indices
    void apply_handles();
    // ---- Georama edit mode (dc_townedit.cpp)
    std::unique_ptr<dccore::Georama> geo_;
    dccore::CameraState edit_cam_;
    float cursor_[3] = {0, 0, 0};
    int edit_area_ = -1, edit_def_ = -1, edit_rot_ = 0, edit_status_ = -1, edit_frames_ = 0, edit_hold_ = 0, edit_rest_ = 0;
    int hand_chip_ = -1, hand_part_ = -1, hand_slot_ = -1, hand_tray_ = -1;   // the tip carried on the Atla board and where it came from
    bool edit_valid_ = false, edit_menu_request_ = false;
    uint32_t edit_prev_bits_ = 0, edit_prev_buttons_ = 0;
    Node3D *edit_ghost_ = nullptr;
    MeshInstance3D *edit_outline_ = nullptr;
    int ghost_def_ = -2;
    float ghost_pos_[3] = {0, 0, 0}, ghost_yaw_ = 0;
    std::vector<std::pair<int, int>> drops_;          // instances falling into place (instance, frames left)
    Array edit_notices_;
    Dictionary modal_request_;                        // an event asked for a screen (name entry, world map, item choice): {kind, args}
    Dictionary shop_request_;                         // a talk ended in a shop: {shop, charge}; the 2D layer takes it and runs the screen
    bool edit_picked_ = false;                        // a part was chosen on the board (kept when the menu closes)
    std::vector<float> outline_sig_;
    std::vector<uint32_t> script_bits_;               // the menu bits of the scripted ticks
    // fishing (dc_townfish.cpp)
    static constexpr int kHookDrop = 9;
    bool last_ok_down_ = false, last_ok_on_ = false;
    bool fish_active_ = false;
    bool fish_auto_ = false;   // tests: the rod casts, strikes and releases by itself
    std::unique_ptr<dccore::FishingSim> fish_sim_;
    int fish_state_ = 0, fish_t_ = 0, fish_idle_ = 0, fish_cast_t_ = 0, fish_hooked_ = -1, fish_hook_window_ = 0, fish_catch_index_ = -1;
    int fish_motion_now_ = 0, fish_motion_ticks_ = 0, fish_bait_item_ = -1, fish_pending_event_ = -1;
    bool fish_line_out_ = false, fish_prev_no_ = false, fish_prev_use_ = false;
    float fish_yaw_ = 0;
    Vector3 fish_hero_, fish_float_;
    Node3D *fish_float_node_ = nullptr, *fish_line_node_ = nullptr, *fish_catch_node_ = nullptr;
    std::vector<Node3D *> fish_nodes_;
    dccore::FishCatch fish_last_catch_;
    std::string fish_catch_text_;
    std::function<void(int)> fish_sound;
    void fish_enter();
    void fish_leave();
    void fish_set_bait(int item);
    void fish_make_nodes();
    void fish_motion(int motion, bool loop);
    void fish_apply();
    void step_fish(const dccore::PadRaw &pad, bool ok_down, uint32_t menu_pressed);
    Vector3 fish_rod_tip() const;
    Vector3 fish_camera_pos() const;
    Vector3 fish_camera_ref() const;
    void tip_changed();
    void edit_enter();
    bool edit_check_to_walk(float out_pos[3]) const;
    void edit_leave(const float pos[3]);
    void step_edit(const dccore::PadRaw &pad, uint32_t bits);
    void apply_edit_log();
    void rebuild_after_edit();
    void update_ghost(bool force_hide);
    void apply_element_frames();
    void edit_notice(const char *kind, int a = 0);
    void edit_limit_cursor(const float prev[3]);
    String atla_name(int id);
    String part_name(int part);
    String chip_name(int chip, bool description);
    std::map<std::string, Ref<ImageTexture>> sprite_textures_;   // textures of the event's sprites by name
    Dictionary sprite_dict(const dccore::EvSprite &s);
    bool event_active(const dccore::TownEvent &e) const;   // map flag (inverted: set = used up) and the time window
    DcCharacter *scene_nodes_[4] = {nullptr, nullptr, nullptr, nullptr};
    double scene_time_[4] = {0, 0, 0, 0};
    float scene_step_[4] = {1, 1, 1, 1};
    void step_scenes();
    float event_fov_ = 0;
    int pending_event_ = -1;

    dccore::PadRaw read_devices(uint32_t &menu_bits) const;
    void apply_state();
    void step_one();
    void step_walk(const dccore::PadRaw &pad, bool ok_down, uint32_t menu_pressed);
    void step_event(const dccore::PadRaw &pad, uint32_t menu_bits);
    void step_talk(bool ok_down, uint32_t menu_pressed);
    bool start_event(int id);
    void finish_event();
    void begin_talk(int person);
    void apply_actors();
    void make_villagers();
    int villager_info_of(int person) const;
    bool villager_present_now(int vil) const;
    DcCharacter *make_actor_node(const std::shared_ptr<dccore::Character> &ch);
    std::shared_ptr<dccore::Pack> pack_at(const std::string &path);
    Vector2 screen_of(int slot) const;

protected:
    static void _bind_methods();

public:
    DcTownWalk();
    ~DcTownWalk();
    void setup(DcTown *scene, DcCharacter *player, const TownEnv &env, int start_event);
    void _physics_process(double delta) override;
    void set_script_ticks(const Array &ticks);
    void set_use_devices(bool on) { use_devices_ = on; }
    void teleport(double x, double y, double z);
    void step_ticks(int n);
    Vector3 get_player_position() const;
    Camera3D *get_camera() const { return camera_; }
    DcTown *get_scene() const { return scene_; }
    int get_tick_count() const { return tick_count_; }
    Array take_nav();                    // [{kind, map, name, event, floor}]: where the script / the player wants to go (see EvNav)
    void set_free_camera(const Vector3 &pos, const Vector3 &target) { free_cam_ = true; free_pos_ = pos; free_ref_ = target; apply_state(); }   // for tools: a fixed view
    int get_near_event() const { return near_event_; }
    void set_fish_auto(bool on) { fish_auto_ = on; }
    Array get_town_events() const;       // tests: {kind, event_no, pos, extent}
    Array get_villagers() const;         // tests: {villager, pos, visible, roamer, moving}
    Dictionary get_eb_ui() const;
    int eb_autoplay_ = -1;
    void debug_eb_autoplay(int offset) { eb_autoplay_ = offset; }   // tests: the EB duel played perfectly       // the EB duel of a story scene (the overlay draws it)
    Dictionary get_ui();                 // windows, fade, prompts for the 2D layer
    int get_mode() const { return int(mode_); }
    int get_bgm_request() const { return bgm_request_; }
    Array take_sound_events() { Array a = sound_events_; sound_events_ = Array(); return a; }   // [{op, a, b, v}] the scripts asked the sound system for
    void set_bgm_number(int n) { bgm_request_ = n; }
    double get_clock() const;            // game time 0..12
    void run_event(int id) { pending_event_ = id; }
    // ---- Georama: the edit mode and the Atla board
    Dictionary take_modal_request() { Dictionary d = modal_request_; modal_request_ = Dictionary(); return d; }
    void modal_done(int value) { if (rt_) rt_->modal_done(value); }
    void refresh_names();                                // the characters' names changed (the name screen)
    Dictionary take_shop_request() { Dictionary d = shop_request_; shop_request_ = Dictionary(); return d; }
    void shop_done() { if (mode_ == ModeShop) mode_ = ModeWalk; }
    bool edit_locked_ = false;   // co-op: only the host builds the Georama
    void set_edit_locked(bool on) { edit_locked_ = on; }
    bool can_edit() const { return geo_ != nullptr && !town_->interior && !edit_locked_; }
    Dictionary get_edit_ui();                            // {active, status, part, part_name, rot, valid, can_leave, menu, over_name, cursor, screen}
    Array take_edit_notices() { Array a = edit_notices_; edit_notices_ = Array(); return a; }   // [{kind, a}]: "place", "remove", "denied", "menu", "enter", "leave"
    void edit_toggle();                                  // Select
    void edit_menu_done(int result);                     // the menu closed: 0 back to the cursor, 1 Move&Remove
    Array georama_board();                               // pages: [{part, name, obtained, max, placed, complete, slots: [{k, chip, name, desc, installed, visible, tile}]}]
    Array georama_tray();                                // the tip tray: chip ids (-1 free)
    int georama_hand() const { return hand_chip_; }
    bool georama_press_slot(int part, int slot);         // Cross on an element slot of the board
    bool georama_press_tray(int cell);                   // Cross on a tray cell
    void georama_cancel_hand();                          // Circle: the tip goes back
    void georama_sort_tray();
    bool georama_select_part(int part);                  // Cross on the page header: place copies of the part
    Array georama_analysis();                            // [overall, complete parts, requests] in percent
    String georama_chip_name(int chip, bool description) { return chip_name(chip, description); }
    String georama_text(int id) { return atla_name(id); }
    void place_hero(double x, double y, double z, double yaw);   // after coming out of a house
    // Co-op: the hero's own model is another character's (the player cycled the characters), and every player's clothes have their colour (-1 original)
    void set_hero_model(DcCharacter *model);
    void set_hero_color(int index);
    int hero_color_ = -1;
    Dictionary get_hero_pose() const;                              // {pos, yaw, motion, time, mode}: what the other players see of the hero
    Dictionary get_door_out(int event_index) const;               // {pos, yaw}: where the hero stands after leaving through that door
};

} // namespace godot
