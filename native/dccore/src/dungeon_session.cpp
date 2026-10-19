#include "dccore/dungeon_session.h"
#include "dccore/weapon_ops.h"
#include <cmath>
#include <cstring>

namespace dccore {

DungeonSession::DungeonSession(const Bytes& event_stb, int dungeon, SaveData* save, UserStatus* status, GameRand* rng, const ItemDb* items, const MessageBank* system_text,
                               const MessageBank* event_bank, EventHost* host)
    : dungeon_(dungeon) {
    if (!host) { host_ = std::make_unique<EventHost>(); host = host_.get(); }
    auto prog = std::make_shared<ScriptProgram>(event_stb);
    rt_ = std::make_unique<EventRuntime>(prog, host);
    rt_->save = save;
    rt_->status = status;
    rt_->items = items;
    rt_->rng = rng;
    rt_->map_no = dungeon;
    rt_->map_dir = "dun/script/";
    if (system_text) rt_->system_bank = *system_text;
    if (event_bank) { rt_->event_bank = *event_bank; rt_->has_event_bank = true; }
    rt_->extra_ext = [this](int id, const ScriptArgs& a) { return dungeon_ext(id, a); };
}

bool DungeonSession::start(int fid, int mode, const float pos[3], bool heavy, int chr_help) {
    if (!rt_->has_function(fid)) return false;
    chr_help_ = chr_help;
    current_ = fid;
    action_mode_ = mode;
    heavy_ = heavy;
    if (pos) for (int k = 0; k < 3; ++k) event_pos_[k] = pos[k];
    if (hold_world) hold_world(true);
    if (hero_state) {   // the hero of the script is the player
        float p[3] = {0, 0, 0}, yaw = 0;
        hero_state(p, yaw);
        for (int k = 0; k < 3; ++k) hero_start_[k] = p[k];
        rt_->actor(-1).pos = {p[0], p[1], p[2]};
        rt_->actor(-1).rot = {0, yaw, 0};
    }
    running_ = true;
    request_end_ = false;
    run_next_ = -1;
    nav_pending_ = false;
    if (!rt_->start(fid)) finish();   // (the function ended at once)
    else if (request_end_) { request_end_ = false; rt_->abort(); finish(); }
    return true;
}

void DungeonSession::finish() {
    if (current_ == 100) floor_start_ = -1;
    running_ = false;
    current_ = -1;
    if (place_hero) {   // a scene that placed the hero leaves him there (the arena of a duel)
        const EvVec3 hp = rt_->actor(-1).pos;
        if (std::fabs(hp.x - hero_start_[0]) > 0.01f || std::fabs(hp.y - hero_start_[1]) > 0.01f || std::fabs(hp.z - hero_start_[2]) > 0.01f) {
            const float p[3] = {hp.x, hp.y, hp.z};
            place_hero(p, rt_->actor(-1).rot.y);
        }
    }
    const EvNav n = rt_->nav();
    if (n.kind != EvNav::None) { nav_ = n; nav_pending_ = true; }
    if (hold_world) hold_world(false);
    if (run_next_ >= 0 && !nav_pending_) {   // _RUN_SCRIPT_NO: the next function follows
        const int f = run_next_;
        const bool heavy = run_next_heavy_;
        run_next_ = -1;
        start(f, 0, event_pos_, heavy);
    }
}

bool DungeonSession::take_nav(EvNav& out) {
    if (!nav_pending_) return false;
    out = nav_;
    nav_pending_ = false;
    return true;
}

void DungeonSession::tick(uint32_t pad_on, uint32_t pad_down, bool player_alive) {
    if (running_) {
        rt_->pad_on = pad_on;
        rt_->pad_down = pad_down;
        if (!rt_->step()) { finish(); return; }
        if (request_end_) {   // the request of _GO_DUNGEON / _RUN_SCRIPT_NO ends the event even if the script is still going
            request_end_ = false;
            rt_->abort();
            finish();
        }
        return;
    }
    rt_->step_fade();   // (a fade left over from the last event still runs out)
    if (!player_alive) return;
    if (take_death_event) {   // a monster with a death event died
        const int f = take_death_event();
        if (f >= 0) { start(f, 0, event_pos_, false); return; }
    }
    if (clean_fid_ >= 0 && monster_count && monster_count() < 1) {   // _CLEAN_MONSTOR_SCRIPT_NO: the floor is clear
        const int f = clean_fid_;
        const bool heavy = clean_heavy_;
        clean_fid_ = -1;
        start(f, 0, event_pos_, heavy);
    }
}

// The frames are shared by the two floors: after the swap the handle of a frame that nobody moved reads where the frame of the NEW floor stands.
// A script that moved a frame (_SET_OBJHDL_POS) set its position in the part's axes (the cart of the back-floor entrance rides along its own z); the world position the
// next _GET_OBJHDL_POS reads is the frame's place on the floor plus that offset turned with the part.
void DungeonSession::refresh_hdl(ObjHdl& hd) {
    if (!hd.ok || !find_frame) return;
    float p[3], yaw = 0;
    if (!find_frame(hd.name, p, yaw)) return;
    hd.rot[0] = hd.roff[0];
    hd.rot[1] = yaw + hd.roff[1];
    hd.rot[2] = hd.roff[2];
    for (int k = 0; k < 3; ++k) hd.pos[k] = p[k];
    if (!hd.moved) return;
    hd.pos[0] += hd.off[0] * std::cos(yaw) - hd.off[2] * std::sin(yaw);
    hd.pos[1] += hd.off[1];
    hd.pos[2] += hd.off[0] * std::sin(yaw) + hd.off[2] * std::cos(yaw);
}

bool DungeonSession::dungeon_ext(int id, const ScriptArgs& a) {
    UserStatus& st = *rt_->status;
    const int n = a.n;
    switch (id) {
        case 22: a.set_int(0, floor_start_ >= 0 ? floor_start_ : -1); return true;                 // _GET_DUNGEON_FLOOR
        case 1010: a.set_int(0, floor_start_ >= 0 ? floor_start_ : st.get<int8_t>(2)); return true;   // _GET_FLOOR_LEVEL
        case 1011:   // _SET_FLOOR_LEVEL: the floor the script names becomes the current one
            if (floor_start_ >= 0) { st.set<int8_t>(2, int8_t(floor_start_)); return true; }
            st.set<int8_t>(3, st.get<int8_t>(2));
            st.set<int8_t>(2, int8_t(a.get_int(0)));
            return true;
        case 1012: a.set_int(0, floor_start_ >= 0 ? -1 : st.get<int8_t>(3)); return true;          // _GET_OLD_FLOOR_LEVEL
        case 1020: a.set_int(0, action_mode_); return true;                                        // _GET_ACTION_MODE
        case 1021: {   // _ITEM_USE_WINDOW(&result, id ...): the player picks an item of the bag (the front end's window answers with the item id, -1 for none); the listed ids are the ones that matter to the script
            std::vector<int> listed;
            for (int k = 1; k < n; ++k) listed.push_back(a.get_int(k));
            rt_->begin_modal(11, listed, a);
            return true;
        }
        case 1022: a.set_int(0, floor_start_ >= 0 ? floor_start_ : st.get<int8_t>(2)); return true;   // _OPEN_ENTRANCE_WINDOW: the floor stays
        case 1023: { const int i = a.get_int(0); a.set_int(1, i >= 0 && i < 50 ? st.raw[0x8aa4 + size_t(i)] : 0); return true; }   // _CHECK_EVENT_FLG
        case 1024: { const int i = a.get_int(0); if (i >= 0 && i < 50) st.raw[0x8aa4 + size_t(i)] = uint8_t(a.get_int(1)); return true; }   // _SET_EVENT_FLG
        case 1040: a.set_float(0, event_pos_[0]); a.set_float(1, event_pos_[1]); a.set_float(2, event_pos_[2]); return true;   // _GET_EVENT_POS
        case 1041: a.set_float(0, 0.0f); a.set_float(1, 0.0f); a.set_float(2, 0.0f); return true;
        case 1030: {   // _GET_OBJHDL(handle, part, "frame"): the part number only narrows the search in the game; the frame names are unique on a floor
            const int hi = a.get_int(0);
            if (hi < 0 || hi >= 32 || n < 3) return true;
            ObjHdl& hd = hdl_[hi];
            hd = ObjHdl{};
            hd.name = a.get_string(2);
            float p[3] = {0, 0, 0}, yaw = 0;
            if (find_frame && find_frame(hd.name, p, yaw)) {
                hd.ok = true;
                for (int k = 0; k < 3; ++k) hd.pos[k] = p[k];
                hd.rot[1] = yaw;
            }
            return true;
        }
        case 1031: {   // _SET_OBJHDL_POS(handle, x, y, z)
            const int hi = a.get_int(0);
            if (hi < 0 || hi >= 32 || n < 4) return true;
            ObjHdl& hd = hdl_[hi];
            for (int k = 0; k < 3; ++k) hd.off[k] = a.get_float(1 + k);
            hd.moved = true;
            refresh_hdl(hd);
            if (hd.ok && move_frame) move_frame(hd.name, hd.off, hd.roff);
            return true;
        }
        case 1032: {   // _SET_OBJHDL_ROT(handle, rx, ry, rz)
            const int hi = a.get_int(0);
            if (hi < 0 || hi >= 32 || n < 4) return true;
            for (int k = 0; k < 3; ++k) hdl_[hi].roff[k] = a.get_float(1 + k);   // (turned with the part, like the position)
            hdl_[hi].moved = true;
            refresh_hdl(hdl_[hi]);
            if (hdl_[hi].ok && move_frame) move_frame(hdl_[hi].name, hdl_[hi].off, hdl_[hi].roff);
            return true;
        }
        case 1033: {   // _SET_OBJHDL_DRAW_FLAG(handle, 1 show / 2 hide)
            const int hi = a.get_int(0);
            if (hi < 0 || hi >= 32 || n < 2) return true;
            hdl_[hi].draw = a.get_int(1);
            if (hdl_[hi].ok && frame_draw) frame_draw(hdl_[hi].name, hdl_[hi].draw == 1);
            return true;
        }
        case 1034: {   // _GET_OBJHDL_POS(handle, &x, &y, &z)
            const int hi = a.get_int(0);
            if (hi < 0 || hi >= 32 || n < 4) return true;
            refresh_hdl(hdl_[hi]);
            for (int k = 0; k < 3; ++k) a.set_float(1 + k, hdl_[hi].pos[k]);
            return true;
        }
        case 1035: {   // _GET_OBJHDL_ROT(handle, &rx, &ry, &rz): (0, the frame's heading, 0)
            const int hi = a.get_int(0);
            if (hi < 0 || hi >= 32 || n < 4) return true;
            refresh_hdl(hdl_[hi]);
            for (int k = 0; k < 3; ++k) a.set_float(1 + k, hdl_[hi].rot[k]);
            return true;
        }
        case 1081: a.set_int(0, item_trap_id ? item_trap_id() : 0); return true;                    // _GET_ITEM_TRAPID
        case 1082: if (reset_item_trap) reset_item_trap(); return true;                             // _RESET_ITEM_TRAP
        case 1083: if (bomb_set) bomb_set(); return true;                                           // _BOM_SET
        case 1084: if (status_err) status_err(a.get_int(0)); return true;                           // _SET_STATUS_ERR
        case 1098: if (ibox_angle) ibox_angle(a.get_float(0)); return true;                         // _SET_IBOX_ANGLE
        case 1099: if (ibox_finish) ibox_finish(); return true;                                     // _SET_IBOX_FINISH
        case 1036: if (set_ura_dungeon) set_ura_dungeon(); return true;                              // _SET_URA_DUNGEON
        case 1073: if (floor_title) floor_title(); return true;                                     // _SET_FLOOR_TITLE
        case 1090: if (hold_item_event) hold_item_event(a.get_int(0)); return true;                 // _HOLD_ITEM_EVENT
        case 1055: if (set_event_sw) set_event_sw(a.get_int(0), a.get_int(1) != 0); return true;   // _SET_EVENT_SW
        case 1056: {   // _SET_MONSTOR_ID(k [, load [, x, y, z [, death_event]]])
            float p[3] = {0, 0, 0};
            const bool has_pos = n >= 5;
            if (has_pos) { p[0] = a.get_float(2); p[1] = a.get_float(3); p[2] = a.get_float(4); }
            if (spawn_monster) spawn_monster(a.get_int(0), n >= 2 && a.get_int(1) != 0, has_pos ? p : nullptr, n >= 6 ? a.get_int(5) : -1);
            return true;
        }
        case 1057: a.set_int(1, 0); return true;                                                    // _CHK_ATRA_HAVE
        case 1059: {   // _SET_IBOX(x, y, z, item)
            const float p[3] = {a.get_float(0), a.get_float(1), a.get_float(2)};
            if (place_item_box) place_item_box(p, a.get_int(3));
            return true;
        }
        case 1060: a.set_int(0, st.character()); return true;                                       // _GET_NOW_USER_ID
        case 1061:   // _RUN_SCRIPT_NO(fid [, heavy [, keep_fade]])
            run_next_ = a.get_int(0);
            run_next_heavy_ = n >= 2 && a.get_int(1) != 0;
            request_end_ = true;
            return true;
        case 1050: request_end_ = true; return true;                                               // _GO_DUNGEON
        case 1064: a.set_int(0, gate_key_item ? gate_key_item() : 0); return true;                  // _GET_GATEKEY_NO
        case 1066:   // _CLEAN_MONSTOR_SCRIPT_NO(fid [, heavy])
            clean_fid_ = a.get_int(0);
            clean_heavy_ = n >= 2 && a.get_int(1) != 0;
            return true;
        case 1068: st.set<int32_t>(0x8b10, a.get_int(0)); return true;                              // _SET_LIMMIT_ZONE
        case 1080: st.set<int32_t>(0x8b10, -1); return true;                                        // _SET_RES_LIMMIT_ZONE
        case 1074: a.set_int(0, ruby_key); return true;                                             // _GET_RUBY_ELEMENT
        case 1079: a.set_int(0, 0); return true;                                                    // _EASTKING_COMPLETE
        case 1086: a.set_int(0, rt_->save ? rt_->save->get<int32_t>(0x12f94) : 0); return true;     // _CHECK_MARDAN (the number of Mardan Garayan caught)
        case 1089: a.set_int(0, chr_help_); return true;                                            // _CHECK_CHR_HELP (1: OK pressed at a character-room marker; 0: a hit started it)
        case 1095: a.set_int(0, dungeon_ + 1); return true;                                         // _GET_MAP_CODE
        case 1097: a.set_int(1, -1); return true;                                                   // _GET_ITEM_UNIT_NO
        case 1100: {   // _GET_PIERO_ITEM(&a, &b)
            int items[2] = {-1, -1};
            if (piero_item) piero_item(items);
            a.set_int(0, items[0]);
            a.set_int(1, items[1]);
            return true;
        }
        default:
            // everything else of the table is presentation or floor-start work that is done elsewhere: the camera, the moving frames (1030..1036), the draw flags, the
            // character switch, the bees, the windows ... (the call is accepted and does nothing)
            return id >= 1000 && id <= 1101;
    }
}

}  // namespace dccore
