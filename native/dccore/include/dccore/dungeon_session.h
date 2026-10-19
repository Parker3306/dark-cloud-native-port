#pragma once
// The story scripts of a dungeon (dun/script/d0N/event.stb): trigger areas on the floor, the death events of monsters, "the floor is cleared" events, boss starts.
// A DungeonSession owns the EventRuntime that runs them (the same VM and the same cut-scene vocabulary as the towns) and answers the 1010..1101 functions of the
// dungeon table (notes/dungeon_events.md section 3). The world the script acts on (markers, monsters, item boxes) is reached through hooks set by the front end.
// What is not presented here: the event camera, the actors of cut-scenes and the moving frames (doors): the script runs, its messages and fades are shown, the
// state it changes (flags, monsters, items, switches, the way on) is real.
#include <array>
#include <functional>
#include <memory>
#include <string>
#include "dccore/event_runtime.h"

namespace dccore {

class DungeonSession : public EventHost {
public:
    bool eb_active() const { return rt_ && rt_->eb.active(); }                              // the EB duel runs: the pad treats the stick as a d-pad
    const EbDuel* eb_state() const { return rt_ ? &rt_->eb : nullptr; }
    DungeonSession(const Bytes& event_stb, int dungeon, SaveData* save, UserStatus* status, GameRand* rng, const ItemDb* items, const MessageBank* system_text, const MessageBank* event_bank,
                   EventHost* host = nullptr);

    // ---- the world the script acts on
    std::function<void(int event_id, bool on)> set_event_sw;                                // CDungeonEventMan::SearchDataSwitch
    std::function<int(int k, bool load, const float* pos, int death_event)> spawn_monster;  // _SET_MONSTOR_ID: view slot or -1
    std::function<void(const float pos[3], int item)> place_item_box;                       // _SET_IBOX
    std::function<int()> monster_count;                                                     // GetMonstorNum()
    std::function<int()> take_death_event;                                                  // CheckEventFlag2: a monster with a death event just died (its function id) or -1
    std::function<void(bool hold)> hold_world;                                              // monsters and effects stand still while a heavy event runs
    std::function<void(float pos[3], float& yaw)> hero_state;                               // where the player stands
    std::function<void(const float pos[3], float yaw)> place_hero;                          // the script put the hero somewhere (_SET_NPC_POS(-1, ...)): the player goes there
    std::function<int()> gate_key_item;                                                     // _GET_GATEKEY_NO
    // object handles (_GET_OBJHDL ...): the named frames of the floor the room scripts move and ask about
    std::function<bool(const std::string& name, float pos[3], float& yaw)> find_frame;      // world position and heading of a frame; false when there is none
    std::function<void(const std::string& name, const float off[3], const float rot[3])> move_frame;   // _SET_OBJHDL_POS / _ROT: a frame (door, cart, switch) is where the script put it: an offset and a turn in the part's own axes
    std::function<void(const std::string& name, bool show)> frame_draw;                     // _SET_OBJHDL_DRAW_FLAG
    std::function<void()> set_ura_dungeon;                                                  // _SET_URA_DUNGEON: main <-> back floor (the host swaps when the tick ends; the script yields right after)
    std::function<void(int event_id)> hold_item_event;                                      // _HOLD_ITEM_EVENT: the markers of that event lose their item-use button (both floors)
    std::function<int()> item_trap_id;                                                      // _GET_ITEM_TRAPID: the trap id of the chest being asked about
    std::function<void()> reset_item_trap;                                                  // _RESET_ITEM_TRAP: the chest is safe now
    std::function<void()> bomb_set;                                                         // _BOM_SET: a bomb goes off at the player
    std::function<void(int code)> status_err;                                               // _SET_STATUS_ERR: BtSetStatusErr + a small damage sphere on the player
    std::function<void(float angle)> ibox_angle;                                            // _SET_IBOX_ANGLE: the lid of the chest
    std::function<void()> ibox_finish;                                                      // _SET_IBOX_FINISH: the chest is used up (sunk, hidden)
    std::function<void(int out[2])> piero_item;                                             // _GET_PIERO_ITEM
    std::function<void()> floor_title;                                                      // _SET_FLOOR_TITLE: the banner
    int ruby_key = 0;                                                                       // BtRubyDoorKey: the element the Ruby crystals need (0 fire .. 4 holy)

    // ---- running
    bool running() const { return running_; }
    // Starts function `fid`: mode 0 stepped on, 1 OK, 2 item-use button; `pos` is the marker that raised it. False when the function does not exist.
    bool start(int fid, int mode, const float pos[3], bool heavy = false, int chr_help = 0);   // chr_help 1: a character-room marker was pressed with OK (only a hint is wanted)
    // One tick: advances the running event (pad bits are the engine's: OK 0x40, cancel 0x20, use 0x80); when none runs, looks for the events the world raises on its own.
    void tick(uint32_t pad_on, uint32_t pad_down, bool player_alive);
    bool has_function(int fid) const { return rt_ && rt_->has_function(fid); }
    // after an event: where the script sent the player (a map jump), if it did
    bool take_nav(EvNav& out);
    // The floor-start script (function 100) of a story room: the map is already built, so what it adds is the room's monsters, switches and scenes. It answers the
    // questions about the floor as if the player had just come down to `floor`.
    bool start_floor(int floor) { floor_start_ = floor; const bool ok = start(100, 0, nullptr, false); if (!ok) floor_start_ = -1; return ok; }
    EventRuntime& rt() { return *rt_; }
    const std::string& handle_name(int i) const { static const std::string none; return i >= 0 && i < 32 ? hdl_[i].name : none; }   // the frame behind a dungeon object handle (BtObjHdl)
    int current_function() const { return current_; }
    int clean_function() const { return clean_fid_; }

private:
    class Host;
    std::unique_ptr<EventHost> host_;
    std::unique_ptr<EventRuntime> rt_;
    int dungeon_ = 0;
    bool running_ = false;
    int current_ = -1;
    int action_mode_ = 0;
    float event_pos_[3] = {0, 0, 0};
    float hero_start_[3] = {0, 0, 0};
    bool heavy_ = false;
    int run_next_ = -1;
    bool run_next_heavy_ = false;
    int floor_start_ = -1;
    int clean_fid_ = -1;
    bool clean_heavy_ = false;
    bool nav_pending_ = false;
    bool request_end_ = false;   // _GO_DUNGEON / _RUN_SCRIPT_NO: the event ends when the VM yields
    EvNav nav_;

    struct ObjHdl { bool ok = false, moved = false; std::string name; float pos[3] = {0, 0, 0}, rot[3] = {0, 0, 0}, off[3] = {0, 0, 0}, roff[3] = {0, 0, 0}; int draw = 1; };   // off: where a script put the frame (in the part's own axes)
    ObjHdl hdl_[32];
    int chr_help_ = 0;
    void refresh_hdl(ObjHdl& hd);
    bool dungeon_ext(int id, const ScriptArgs& a);
    void finish();
};

}  // namespace dccore
