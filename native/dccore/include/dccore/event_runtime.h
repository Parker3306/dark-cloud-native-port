#pragma once
#include "dccore/eb_duel.h"
// The event / cutscene runtime of the towns, house interiors, story scenes (and later the dungeons): one script VM (script.h) running a function of a map's
// event.stb, the ~370 external functions the scripts call (notes/event_ext.csv), and the state those functions drive: actors (the hero and the villagers),
// action sequences (ASQ), the event camera, message windows, fades, flags. Everything that is only presentation or file access goes to an EventHost; the
// front end reads the actors / camera / windows each frame to draw them. Ported from the decompiled Ed* functions, see notes/events.md.
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "dccore/dungeon.h"
#include "dccore/items.h"
#include "dccore/savedata.h"
#include "dccore/script.h"
#include "dccore/text.h"
#include "dccore/user_status.h"
#include "dccore/walk.h"

namespace dccore {

struct EvVec3 {
    float x = 0, y = 0, z = 0;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// Action sequences (CActionSeq): a queue of timed commands that moves and animates one character.

struct AsqNode {
    int type = 0;                 // see AsqType
    int value = 0;                // ticks / count
    float v[4] = {0, 0, 0, 0};    // vector (x, y, z) / speed in v[3]
    int a = 0, b = 0;             // motion: motion number, flags / anime: number, on
    float speed = 0;              // motion speed
    int mflags = 0;               // motion: flags (bit 4)
    int kind = 0;                 // motion kind: 5 play, 6 stop, 7 next
};

enum AsqType { kAsqSetPos = 1, kAsqMove = 2, kAsqRotRef = 4, kAsqRotMove = 5, kAsqRotAngle = 6, kAsqClearRot = 7, kAsqWaitRot = 8, kAsqSetRot = 10, kAsqDelayRot = 0xb,
               kAsqMotion = 0xc, kAsqMotionTrg = 0xe, kAsqAnime = 0xf, kAsqAnimeTrg = 0x12 };

struct EvActor;

class ActionSeq {
public:
    int chara = -2;               // bound actor slot (-1 hero), -2 none
    std::deque<AsqNode> move, motion, anime;
    float pos[4] = {0, 0, 0, 0}, prev[4] = {0, 0, 0, 0};
    float rot[4] = {0, 0, 0, 0}, rot_prev[4] = {0, 0, 0, 0};
    float rot_target[4] = {0, 0, 0, 0};   // target point / angle / speed in [3]
    float last_target[4] = {0, 0, 0, 0};  // where the last queued move ends (the position a 2-argument MOVE goes to)
    int move_tick = 0, anime_tick = 0;
    int rot_mode = 0;             // 0 none, 1 face a point, 2 face the movement, 3 face an angle
    bool rot_done = false;
    int rot_delay = 0;
    bool motion_on = false;
    int motion_delay = 0;
    bool anime_on = false;
    int anime_delay = 0;

    void clear();
    bool idle() const;            // CheckEnd
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// Characters of the event: slot -1 is the hero, 0.. the villagers / actors.

struct EvActor {
    bool loaded = false;
    std::string pack, entry;      // where the model came from (pack file name, cfg entry)
    int generation = 0;           // changes whenever the model is replaced
    EvVec3 pos, rot, scale{1, 1, 1};
    bool draw = true, shadow = true, collide = false, stop = false, draw_before = false;
    int foot_sound = 1;
    int motion = 0;
    float motion_speed = -1.0f;
    int motion_flags = 0;
    uint32_t motion_serial = 0;   // bumped by every motion request
    float body[3] = {5, 20, 5};   // radius, height, depth
    float ambient[4] = {128, 128, 128, 128};
    uint32_t anime_mask = 0;      // texture animation sets turned on (bit n = set n)
    int attach_kind = 0;          // 0 none, 1 scene frame, 2 actor frame, 3 item
    int attach_target = -2;
    std::string attach_frame;
    int villager_info = -1;       // town villagers
    int info_id = -1;
};

struct EvScene {
    bool loaded = false;
    std::string pack, entry;
    float speed = 1.0f;
    EvVec3 pos, rot;
    bool camera_sync = false;
};

struct EvHandle {                 // _HOBJ_*: a named frame of a map object / actor that scripts move and hide
    int kind = 0;                 // 0 none, 1 fixed part, 2 edit part, 3 interior part, 4 actor, 5 item
    int index = -1;
    std::string frame;
    std::vector<int> bt;          // kind 6 (_HOBJ_BT_HOBJ): the dungeon object handles (BtObjHdl) this group animates
    EvVec3 pos, rot, scale{1, 1, 1};
    bool draw = true;
    bool moved = false;
    bool pos_set = false, rot_set = false;   // which of the two a script (or an _OBJ_ANIME) wrote: a frame keeps what was not written
};

struct EvCamera {
    CameraState cs;               // the game's CCamera + CCameraFollow (pos/ref glide toward next_pos/next_ref, or orbit a follow point)
    int follow_chara = -2;        // keep following this actor (-1 hero)
    float projection = 0, roll = 0;
    int sync_ref_chara = -2;
    EvVec3 sync_ref_offset;
    int sync_ref_obj = -1, sync_pos_obj = -1;
    EvVec3 sync_obj_offset;
    int reset_mode = 0;           // _RESET_CAMERA: after the event > 0 follow the hero again, < 0 keep the scripted camera
    float reset_angle = 3.14159f;
};

struct EvSprite {                 // _SP_*: one of 16 persistent sprites; _DRAW_SPRITE entries use the same fields
    bool active = false, show = false, background = false;
    std::string tex;
    int u = 0, v = 0, w = 0, h = 0;
    float x = 0, y = 0, sw = 0, sh = 0;   // screen position (640 x 448) and size (0 = the source rectangle)
    float rgba[4] = {128, 128, 128, 128};
    float rot = 0, rot_cx = 0, rot_cy = 0;
    float vx = 0, vy = 0;
    bool bilinear = false;
};

struct EvLight {
    bool on = false;
    float dir[3] = {0, 0, 0}, color[3] = {128, 128, 128};
};

struct EvFade {
    bool active = false;
    bool out = false;
    int r = 0, g = 0, b = 0;
    float alpha = 0;              // 0..128
    float step = 0;
    bool end = true;              // fade_end: the last fade has finished
};

// A message window (ClsMes): the state machine of 4.2 of notes/events.md; the front end draws it.
class MesWindow {
public:
    enum State { Closed = 0, Opening = 1, Printing = 2, Complete = 3, Closing = 4, PageWait = 5 };
    struct Page {
        std::string text;                     // the page as shown (names and numbers substituted)
        int glyphs = 0;                       // glyphs that count for the reveal
    };

    const MessageBank* bank = nullptr;
    const MessageBank* system_bank = nullptr;
    std::vector<std::string> hero_names;
    int message_id = -1;
    std::vector<Page> pages;
    size_t page = 0;
    float reveal = 0;                         // glyphs shown on this page
    float speed = 0.3f, default_speed = 0.3f; // glyphs per tick (0 = all at once)
    float openness = 0;                       // 0..1
    bool want_open = false;
    bool waiting_key = false;
    // style
    int pos_mode = 0, tail = 0, fukidashi = 0, cursor = -1, win_flag = 0;
    bool okuri = true;
    int autoset_a = -2, autoset_b = -2;       // characters the window is placed between
    bool autoset_rect = false;
    int rect[4] = {0, 0, 0, 0};
    int tail_w[2] = {0, 0};
    bool open_flag_set = false;

    void make(int id, int number, const std::vector<int>& names, const std::vector<int>& values);
    void step();                              // Step() + the reveal of MyTextureMake
    int state() const;
    void next_page();
    void close();
    std::string visible_text() const;         // the revealed part of the current page
    const std::string& page_text() const { static const std::string empty; return page < pages.size() ? pages[page].text : empty; }
    bool last_page() const { return page + 1 >= pages.size(); }
    int number = 0;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

struct EvNav {                    // what the script asked for when it ended with a navigation code
    enum Kind { None, Interior, Outside, MapJump, Dungeon, Fishing } kind = None;
    int map = 0;                  // MapJump: 1-based map number
    int event = -1;               // start event of the destination
    std::string name;             // Interior: name of the house
    int parts = -1;
    int floor = -1;
};

// Everything the runtime cannot do by itself. All methods have harmless defaults so that scripts run headless.
class EventHost {
public:
    virtual ~EventHost() = default;
    // loading of the event's resources (packs are .chr files, entries are the cfg/model/motion files inside them)
    virtual bool load_pack(int index, const std::string& path) { (void)index; (void)path; return true; }
    virtual bool load_actor(int slot, int pack, const std::string& entry) { (void)slot; (void)pack; (void)entry; return true; }
    virtual void unload_actor(int slot) { (void)slot; }
    virtual bool load_main_chara(const std::string& file, const std::string& cfg, int mode) { (void)file; (void)cfg; (void)mode; return true; }   // _LOAD_MAIN_CHARA: the hero's model for the event (the fishing rod)
    virtual bool load_scene(int scene, int pack, const std::string& entry, float speed) { (void)scene; (void)pack; (void)entry; (void)speed; return true; }
    virtual void load_textures(int block, int pack, const std::vector<std::string>& entries) { (void)block; (void)pack; (void)entries; }
    virtual void load_villager(int slot, int info_id, const std::string& file) { (void)slot; (void)info_id; (void)file; }
    // villagers of the town
    virtual int villager_present(int vil) { (void)vil; return 0; }
    virtual void villager_appear(int op, int vil) { (void)op; (void)vil; }    // 0 on, 1 off, 2 in, 3 out
    virtual void villagers_load() {}
    virtual int villager_part(int vil) { (void)vil; return -1; }
    // Georama
    virtual int georama_complete(int parts) { (void)parts; return 0; }
    virtual int georama_cmp_event(int parts) { (void)parts; return 0; }
    virtual void georama_set_cmp_event(int parts, int v) { (void)parts; (void)v; }
    virtual int georama_request(int parts) { (void)parts; return 0; }
    virtual int georama_atra_parts(int map) { (void)map; return 0; }
    virtual int georama_request_percent(int town1) { (void)town1; return 0; }            // _SGET_REQUEST
    virtual void georama_atra_get(int kind, int town1, int id) { (void)kind; (void)town1; (void)id; }   // _SATRA_PARTS_GET (kind 0) / _SATRA_CHIP_GET (kind 1)
    virtual void georama_set_request_flag(int town1, int v) { (void)town1; (void)v; }   // _SSET_REQUEST_EVENT_FLAG
    virtual bool georama_parts_pos(int obj, EvVec3& pos, EvVec3& rot) { (void)obj; (void)pos; (void)rot; return false; }
    // the motion state of an actor (for the "play, then go on" nodes of the action sequences): start / end / blend frames of a motion, and the frame it is at
    // the cfg row (KEY start, end, speed) of a motion of the actor in `slot` (-1 the hero); false when the actor has no such motion (the EB duel then does not start)
    virtual bool actor_motion_range(int slot, int motion, float& start, float& end, float& speed) { (void)slot; (void)motion; (void)start; (void)end; (void)speed; return false; }
    virtual bool actor_motion_info(int slot, int motion, float& start, float& end, float& blend) { (void)slot; (void)motion; start = 0; end = 10; blend = 0; return true; }
    virtual float actor_motion_time(int slot) { (void)slot; return 9.995f; }
    // sound (ids are the game's bank numbers)
    virtual void sound(const char* op, int a, int b, const float* v) { (void)op; (void)a; (void)b; (void)v; }
    virtual int bgm_playing() { return -1; }
    virtual bool sound_loading() { return false; }
    virtual bool bgm_fading() { return false; }
    // items / system messages
    virtual void item_got(int item) { (void)item; }
    // modal screens (answered with EventRuntime::modal_done)
    virtual void open_modal(int kind, const std::vector<int>& args) { (void)kind; (void)args; }
    // a script wants to be told when it logs something unknown
    virtual void unknown_ext(int id, const char* name) { (void)id; (void)name; }
};

class EventRuntime {
public:
    EventRuntime(std::shared_ptr<ScriptProgram> program, EventHost* host);

    // game state the scripts read and write
    SaveData* save = nullptr;
    UserStatus* status = nullptr;
    const ItemDb* items = nullptr;
    GameRand* rng = nullptr;
    int map_no = 0;                    // NowEditMap: index of the map in the save data (0..79)
    int old_map_no = 0;                // OldMapNo
    std::string map_dir;               // "gedit/e01/"
    int talk_npc_info = -1;            // talk_chara_info_id (the villager being talked to)
    int interior_flag = 0;             // EdInteriorFlag
    // pad (engine bit layout: OK 0x40, cancel 0x20, ...), set by the front end before each step
    uint32_t pad_on = 0, pad_down = 0, pad_up = 0;
    EbDuel eb;                                // the EB duel mini-game (_EB_* functions)
    float stick[4] = {0, 0, 0, 0};     // lx ly rx ry in -1..1

    MessageBank event_bank, talk_bank, system_bank;   // bank of the event script (win 1), of the villagers' chat (win 0), system text
    bool has_event_bank = false, has_talk_bank = false;
    float msg_speed = 0.3f;            // the option MESSAGE DSPLY SPEED: 0.3 normal, 0.6 speedy glyphs per tick
    std::vector<std::string> hero_names = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

    // ---- running
    // EdEventInit: false when the function does not exist or finished at once.
    bool start(int function_id);
    // one frame of EdEventMode; false when the event is over
    bool step();
    bool running() const { return vm_.running(); }
    bool has_function(int id) const { return vm_.has_function(id); }
    void skip();
    void abort() { vm_.abort(); }
    int result_code() const { return result_code_; }
    void step_fade();                                  // EdFadeInOut: runs every frame, also outside events
    EvNav nav() const { return nav_; }
    int next_event() const { return next_event_; }        // _FADEOUT_TO_EVENT: event to run after this one ends (-1 none)
    void item_message(int item);                       // the "You got ..." bar of the system message window
    void modal_done(int value);
    // A modal screen opened by a function of another table (the dungeon's _ITEM_USE_WINDOW): argument 0 of the call (a pointer) receives the front end's answer (modal_done);
    // the script's next `yield` waits for it.
    void begin_modal(int kind, const std::vector<int>& args, const ScriptArgs& a);
    bool in_modal() const { return menu_mode_ != 0; }
    int modal_kind() const { return menu_mode_; }
    const std::vector<int>& modal_args() const { return modal_args_; }

    // the pond the scripts describe (_LOAD_FISHING_DATA, _INIT_FISH, _SET_FISHING_ESA) for the fishing mode that _GOTO_FISHING starts
    struct FishingSetup {
        bool loaded = false;
        int pond_set = 0;
        float rect[4] = {0, 0, 0, 0};       // x0, z0, x1, z1 of the pond
        float water = 0, ground = -30;
        float area[4] = {0, 0, 0, 0};       // where the fish are kept
        int bait_item = -1;                 // _SET_FISHING_ESA: the bait the script put on the hook (an item id)
        bool bait_changed = false;
        bool exit_requested = false;        // _EXIT_FISHING
    } fishing;
    // ---- state the front end draws
    std::array<EvActor, 17> actors;    // index = slot + 1
    EvActor& actor(int slot) { return actors[size_t(slot + 1)]; }
    const EvActor& actor(int slot) const { return actors[size_t(slot + 1)]; }
    std::array<EvScene, 4> scenes;
    std::array<EvHandle, 32> handles;
    // _OBJ_ANIME: 16 tweens of a handle's rotation (kind 0, radians), position (1) or scale (2); modes 0 keep adding the speed, 1 start again at the end, 2 bounce, 3 stop at the end
    struct ObjAnime {
        bool active = false;
        int handle = -1, kind = 0, mode = 0;
        float cur[3] = {0, 0, 0}, start[3] = {0, 0, 0}, end[3] = {0, 0, 0}, speed[3] = {0, 0, 0};
    };
    std::array<ObjAnime, 16> obj_anime;
    EvCamera camera;
    EvFade fade;
    std::array<EvSprite, 16> sprites;
    std::vector<EvSprite> draw_sprites;     // _DRAW_SPRITE / _DRAW_BG_SPRITE entries (cleared by _INIT_SPRITE)
    bool filter_on = false;
    float filter[4] = {0, 0, 0, 0};         // _SCREEN_FILTER r g b a (a: 0..128)
    bool back_on = false;                   // _DRAW_BACK: clear colour while on
    float back[3] = {0, 0, 0};
    bool ambient_on = false;
    float ambient[3] = {60, 60, 60};
    std::array<EvLight, 4> lights;          // _SET_LIGHT overrides
    int dof_level = 0;
    bool thunder = false;
    float wind[4] = {0, 0, 0, 0};
    bool shadows_on = true;
    std::vector<std::string> sprite_textures;   // names loaded by _LOAD_SPRITE_TEXTURE / _LOAD_TEXTURE (for tools)
    std::array<MesWindow, 8> windows;
    std::array<ActionSeq, 10> asq;
    // talk mode (filled by the function 256)
    std::vector<int> talk_list;        // message ids; entry 0 is the plain message or the choice targets
    int talk_kind = -1;                // _SET_TALK_SELECT_MES kind
    int talk_header = -1;
    // the system message bar (item got ...)
    struct SysMes { bool active = false; std::string text; };
    SysMes system_message;
    int system_message_ticks = 0;       // the bar opened by the town closes by itself
    bool in_event_message = false;
    bool exclamation = false;          // _DRAW_EXCLAMATION_MARK this frame
    bool day_banner = false;
    bool draw_back_off = false;
    EvVec3 world_pos, world_rot;       // _SET_WORLD_COORD
    // flags of the event
    int work_flags[32] = {};
    std::set<int> unknown_ext_ids;     // external functions that were called but are not implemented (for tests)
    void eb_sounds() {                 // the sounds the EB duel asked for go to the front end
        for (const EbSound& s : eb.take_sounds())
            host_->sound(s.kind == EbSound::Se ? "_EB_SE" : (s.kind == EbSound::BgmFade ? "_EB_BGM_FADE" : "_EB_FANFARE"), s.a, 0, nullptr);
    }
    uint64_t frames = 0;

    EvVec3 to_world(EvVec3 p) const;
    EvVec3 from_world(EvVec3 p) const;
    float world_yaw(float local_yaw) const { return local_yaw + world_rot.y; }

    // time of day (ConvertTime / InvertTime, EdGetTime)
    static float convert_time(float hours);
    static float invert_time(float t);
    int time_bucket() const;

    // functions of another table (the dungeon's 1010..1101) are answered by the front end: returns true when it handled the call
    std::function<bool(int id, const ScriptArgs& a)> extra_ext;
    // trace of the external calls (id, name) for tools
    std::function<void(int id, const char* name, const ScriptArgs& a)> trace;

private:
    EventHost* host_;
    std::shared_ptr<ScriptProgram> program_;
    ScriptVm vm_;
    int result_code_ = 0;
    EvNav nav_;
    int next_event_ = -1;
    int menu_mode_ = 0;
    std::vector<int> modal_args_;
    int modal_result_slot_ = -1;       // stack slot of the pointer the result goes to
    std::string current_dir_;
    std::vector<std::string> packs_;   // chr_file[0..15]
    int active_file_ = 0, active_buffer_ = 0;
    bool skip_enable_ = false;
    int interior_start_event_ = -1;

    bool ext(int id, const ScriptArgs& a);
    void reset_event_state();          // EdInitEventParam
    void asq_play(ActionSeq& q);
    void step_camera();
    void step_obj_anime();
    void step_actors();
    EvVec3 actor_pos_world(int slot) const;
    friend class ActionSeq;
};

// Test helper: the name of an external function id ("" if unknown).
const char* ev_ext_name(int id);

}  // namespace dccore
