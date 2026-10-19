#pragma once
// Walking through a dungeon floor the way the game does it: player movement (MoveChara, free-move state), the follow camera (CCamera / CCameraFollow) and
// the dungeon's camera rules (autoCamTrial, CameraAutoMove). Everything is stepped once per game tick (60 per second).
// Ported from the decompiled dungeon overlay; see notes/native-plan.md, Milestone 3 phase B.
#include <array>
#include <cstdint>
#include <functional>
#include <vector>
#include "dccore/collision.h"
#include "dccore/character.h"
#include "dccore/combat.h"
#include "dccore/dungeon.h"
#include "dccore/lockon.h"
#include "dccore/floor.h"
#include "dccore/motion_state.h"
#include "dccore/items.h"
#include "dccore/user_status.h"

namespace dccore {

// Raw pad as the game reads it: sticks are bytes (128 = centre), up = low values.
struct PadRaw {
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    uint32_t buttons = 0;     // On(): bit 0 = camera behind the player, bit 2 (value 4) / 3 (value 8) = turn camera
};

constexpr uint32_t kPadBehind = 1, kPadCamRight = 4, kPadCamLeft = 8, kPadOK = 0x40, kPadNO = 0x20, kPadUse = 0x80, kPadItemLeft = 0x8000, kPadItemRight = 0x2000;   // kPadOK: the attack button (PadInput_OK)

// CGamePad::AxisCalibration: 50-unit dead zone, then a linear ramp to +-128; result / 128 is the stick value in -1..1 (up = negative).
float pad_axis(int raw);

// Camera = CCamera (pos/ref smoothing toward next pos/ref) + CCameraFollow (orbit around a follow point).
struct CameraState {
    float pos[3] = {0, 0, 0}, ref[3] = {0, 0, 0};
    float next_pos[3] = {0, 0, 0}, next_ref[3] = {0, 0, 0};
    float speed = 8.0f;           // smoothing divisor (1 = jump)
    int clamp = 0;                // 1: the position moves at most 2 units per axis per tick
    float eps = 0.01f;            // snap distance
    float angle_h = 0, angle_v = 0;   // derived from pos/ref
    float follow[3] = {0, 0, 0};
    float dist = 60.0f, height = 20.0f;
    float angle = 0, angle_cur = 0;   // wanted / current orbit angle
    bool follow_on = true;
};

struct PlayerState {
    float pos[3] = {0, 0, 0};
    float vy = 0;                 // vertical velocity (DAT_01dc2554)
    float yaw = 0;                // facing angle (rotation about +Y; 0 = looking along +Z)
    float disp[3] = {0, 0, 0};    // displacement of the last tick (ref_off)
    int motion = 0;               // keyCtrl result: 0 idle, 1 run, 2 walk
    float motion_speed = 0;       // keyCtrl's animation speed
    MoveInfo info;
};

// What the player is doing and has been hit by (the DAT_01dc44xx globals of MoveChara).
struct PlayerCombat {
    int mode = 0;                 // DAT_01dc4490: 0 free, 1 attacking, 2 throwing, 3 using an item, 4 hurt, 5 hurt and knocked down, 6 guarding
    int action = 0;               // DAT_01dc4494: which step of the current action (combo step, charge, ...)
    int wish = 0;                 // DAT_01dc448c: the motion the player wants to play
    int invuln = 0;               // DAT_01dc44a0: frames of invulnerability left after a hit
    int busy = 0;                 // DAT_01dc44a4: other lock (revive, big attacks)
    int frozen = 0;               // DAT_01dc4518: turned to stone: no input
    int can_act = 1;              // DAT_01dc44e4
    int held = 0;                 // DAT_01dc44f0
    int lock_on = 0;              // lockOnTargetFlag (a copy of LockState::flag at the start of the tick)
    int guard_walk = 0;           // DAT_01dc44e0: 5 while the guard is up (he can still walk, slowly)
    float blow[3] = {0, 0, 0};    // blowVelo: knock-back velocity, decays every tick
    float stick_vector = 0;       // stickVector: size of the stick input (+ vertical velocity)
    float velo = 0, velo_z = 0;   // velo / DAT_01dc2558: wanted horizontal velocity this tick
    bool no_interpolate = false;  // driveNoInterpolate: the next motion change does not blend
    int requested = -1;           // DAT_01dc4484: the motion last sent to the CCharacter
    int unit_hold = 0;            // CMonUnitHold
    float blow_decay = 0.018f;    // DAT_002a1d24
    bool dead = false;            // life ran out: the death sequence (gameTask 200) takes over
    int game_task = 0;            // MoveChara's state: 0 normal play, 200 death animation, 0xc9 / 0xca game over (the rest of the states are not ported)
    bool game_over = false;       // the death animation has finished
    // ---- attacking (ToanKey_On / ToanKey_Play and the counters motionDrive keeps)
    float gauge = 100.0f;         // DAT_01dc44c8: the attack gauge; each combo step costs 25, it refills over time
    int gauge_full = 0;           // DAT_01dc44cc
    int combo_window = 10;        // DAT_01dc4578: counts up to 10 after a swing
    int idle_counter = 0xe10;     // DAT_01dc4560
    int in_swing = 0;             // DAT_01dc4568: the combo can be continued during this motion
    int chain = 0;                // DAT_01dc4570: the button was pressed again in time: continue the combo
    float turn_to = 0;            // DAT_01dc4574: facing the next swing turns to
    int released = 0;             // DAT_01dc4498: the attack button was let go
    float charge = 1.0f;          // DAT_01dc449c: charge attack level (1..3)
    int charge_stage = 0;         // DAT_01dc44dc
    int cooldown = 0;             // DAT_01dc45cc: frames before a new attack can start
    int hit_window = 0;           // DAT_01dc45ca: a swing is in its damaging frames (no gauge refill)
    float lunge = 0, lunge_dec = 0;   // DAT_01dc4590 / 4594: forward step of a swing
    float lunge_dir[3] = {0, 0, 0};   // DAT_01dc4580
    int event_lock = 0;           // DAT_01dc45c8
    int leap = 0;                 // DAT_01dc44c0: a leap attack moves the player along `leap_v`
    float leap_v[3] = {0, 0, 0};  // DAT_01dc44b0 / 44b4 / 44b8
    int heal_counter = 0;         // heal_counter_4569
    float height_above = 0;       // DAT_01dc44c4: height of the feet over the floor found under them
    int shot_wait = 0;            // wait_915: ticks the charged attack of Ungaga has been held
    int effect_id = -1;           // ruby_effect_id: the charge effect instance of Ruby
    int overheat = 0;             // DAT_01dc4524: Osmond's gun has run dry and must refill first
    int osmond_s_cnt = 0, osmond_msg_cnt = 0, osmond_wait = 0, osmond_dmg_wait = 0;   // the function-static counters of Osmond's attack code
    float recoil[2] = {0, 0};     // DAT_01dc4598 / 459c: camera / body recoil of Goro's attacks
    int recoil_time = 0;          // DAT_01dc45a4
    float stick_dir[2] = {0, 0};  // DAT_01dc4540 / 4548: the stick direction rotated by the camera
};

struct WalkParams {
    float run_speed = 1.45f;
    float cam_near = 10.0f, cam_far = 80.0f, cam_up_near = 60.0f;   // camera_near_dist / camera_far_dist / camera_up_near_dist (values read from the live game)
    float follow_height = 7.5f;   // DAT_01dc2764 (per character)
    float follow_height2 = 0.0f;  // DAT_01dc45a0
    int mask = 1;                 // polygon types ignored when moving (Osmond: 8)
    bool camera_collision = true;
};

// The inputs of one tick are `pad`; the world supplies polygons. Returns nothing: state is updated in place.
class Walker {
public:
    Walker(const Dungeon* dungeon, const FloorLayout* layout, WalkParams params = {});

    PlayerState player;
    CameraState camera;
    WalkParams params;

    // Puts the player on the start cell and the camera behind them, as the game does when a floor begins.
    void place_at_start();
    bool place_at_stairs();   // a random floor is entered at its in-stairs (cell flag 0x80, frame `in_2` at the cell centre): the hero stands 2 units in front of it; false when the floor has none
    void place_at(float x, float z, float y = 30.0f);   // the same at any world position (the player drops onto the floor from y)
    // Other worlds (towns) supply their own polygons: what PickUpPoly / PickUpCameraPoly gather around a position. Without them the dungeon floor is used.
    std::function<std::vector<CCPoly>(float x, float y, float z)> poly_source, camera_source;
    void tick(const PadRaw& pad);

    // ---- combat (set the pointers to take part in fights; the motion table is the player model's base.cfg KEY table)
    PlayerCombat combat;
    ColPool* pool = nullptr;
    UserStatus* status = nullptr;
    std::vector<MotionInfo> motion_table;
    MotionRequest request;
    MotionState motion;
    int motion_phase = 0;
    std::function<void(const float pos[3], float vel[3], int character)> monster_collide;   // MoveCheck against the monsters (changes the step)
    std::function<float()> nearest_monster;   // SetBattleStyle: distance to the nearest monster that is alive (10000 when none)
    std::function<void(int slot, int damage, int stolen_gold)> on_monster_hurts_player;   // lets the monsters know (hit_vol / stolen_gold)
    // motionDrive for the player: the motion request follows `combat.wish`; the draw part advances the motion clock
    void update_motion_request();
    void step_motion();
    // CUserStatus::Step (life that drains smoothly after a hit), part of the HUD draw
    void step_status();
    int check_damage();             // BtCheckDamageProc: 1 when the player was hit
    // The weapon: the player model (for the pose of the hand), where the weapon frame sits on its joint and where the blade's hit sphere is on the weapon.
    const Character* model = nullptr;
    int weapon_frame = -1;                    // the model's own "weapon" object (an animated joint)
    float weapon_tip_local[3] = {0, 0, 0}, weapon_tip2_local[3] = {0, 0, 0};   // the weapon's hit points (Toan / Goro: dcol1 / dcol0 in the weapon's own space; Ungaga uses both)
    float ruby_pos[3] = {0, 0, 0}, ruby_vec[3] = {0, 0, 0};   // the globals Ruby's second ball reuses
    bool ruby_shot_valid = false;
    const Character* weapon_model = nullptr;  // the weapon's own model when it has a skeleton (Xiao's slingshot: its frame "eff30" is the muzzle)
    int muzzle_joint = -1;                    // index of "eff30" in the weapon model
    float weapon_muzzle[4] = {0, 0, 0, 1};    // the muzzle in the world as of the last drawn frame
    float weapon_tip[4] = {0, 0, 0, 1}, weapon_tip2[4] = {0, 0, 0, 1}, weapon_origin[4] = {0, 0, 0, 1};   // (weapon_origin: the "weapon" frame itself)      // ... in the world as of the last drawn frame (what the attack code reads)
    PoseSample last_sample;                   // pose the last motion step belonged to
    void setup_weapon(const Character* player_model, const float tip_local[3], const float tip2_local[3] = nullptr, const Character* weapon_model = nullptr);
    void step_shake(GameRand& rng);   // the camera shake of heavy attacks (part of the effects step: one rand() draw per tick while it lasts)
    void add_drink(float param, int ch, int16_t delta);          // CUserStatus::AddDrink (the water gauge)
    void heal(int ch, int16_t delta, float param = 100.0f) { add_now_life(param, ch, delta); }   // AddNowLife for items
    void apply_status_err(int code) { set_status_err(code); }   // BtSetStatusErr: 4 stone, 8 poison, 0x10, 0x20, 0x40 (for traps and the like)
    const float* prev_pos() const { return char_pos_; }   // where the player was at the end of the previous frame (CCharacter position vector 0x1ea1d30)   // Toan's weapon attach (values read from the live game)
    GameRand* rng = nullptr;
    bool drive_step_hold = false;   // driveStepHold
    uint32_t prev_buttons = 0;
    int no_floor_ticks = 0;         // test rigs: the game's collision list is stale for the first ticks after the player is teleported; no polygons for this many ticks

    // The main weapon effect (NowMainEffect: the shot a charged attack sends out) and the weapon wear check (SwordDmgCheck1); supplied by the world that has the shot system.
    // A slingshot bullet (CSHOT): position, velocity per tick, damage
    struct Bullet {
        float pos[3], vel[3];
        int damage;
    };
    std::function<void(const Bullet&)> fire_bullet;
    struct MainShot {
        float pos[3] = {0, 0, 0}, target[3] = {0, 0, 0};
        int owner_kind = 0, sub_id = 0, damage = 0, wait = 0;
        int force_phase = -1;          // Set's last argument: start in this phase (-1: the effect's own first phase)
        bool attach = false;           // hang on the character's "item" frame instead of flying (Ruby's charge effect)
        bool set_attribute = true;     // SetAttribute: the weapon's element goes into the effect row
        bool set_damage = true;        // SetWepStatus / SetVsMonster / SetDmg follow the Set
        int loop = -1;                 // SetLoop
        float random_rate = -1.0f;     // SetRandomRate (jitter at draw time, uses rand())
        bool no_sound = false;
    };
    std::function<int(const MainShot&)> main_shot;     // returns the effect instance (-1 none)
    std::function<void()> end_main_shot;
    std::function<void(int instance)> off_main_effect; // OffEffect(NowMainEffect, instance)
    // Osmond: machine gun bullets (CSHOT_MACHINGUN), the flame thrower (CSHOT_FIREBAR)
    std::function<void(const float pos[3], const float vel[3], int damage)> fire_mg;
    std::function<void(const float pos[3], const float dir[3])> firebar_init;
    std::function<void(const float pos[3], const float dir[3], int damage, int element)> firebar_set;
    std::function<void()> firebar_reset;
    int osmond_type = 0;                               // DAT_01dc4520
    float lock_target[3] = {0, 0, 0};                  // DAT_01dc4500: where the locked-on monster is
    const ItemDb* item_db = nullptr;                   // item effects of the quick-use slots
    int active_sel = 1;                                // itemNowSel: the quick-use slot in hand (1..3)
    LockState* lock = nullptr;                         // the lock-on state shared with the monsters' world
    std::function<bool(int monster, float out[3])> monster_pos;   // where a monster is (GetPosition)
    std::function<int(int shift)> shift_target;        // SetNearLockOnTarget(shift): the next monster in reach
    std::function<void(float)> sword_dmg;
    // Throwing items (BattleActionThlow): aiming with the reticle, the throw animation and the release; the flight itself belongs to the monsters' world.
    // First person view (gameTask 10, `viewMode`): R2 enters, R2 / L1 / circle or a hit leaves; the sticks turn the view, nothing walks.
    bool view_mode = false;
    float view_h = 0, view_v = 0;                      // viewAngleH / viewAngleV
    int throw_aim = 0;                                 // DAT_01eb6304: the reticle is shown and moves with the stick
    int throw_state = -1;                              // DAT_01eb6308: -1 aiming, 1 waiting for the release frame, 2 follow-through
    float reticle[4] = {0, 0, 0, 1};                   // BombInfo: where the item is aimed
    float item_origin[4] = {0, 0, 0, 1};               // the "item" frame of the character in the world (the hand), as of the last drawn frame
    int item_frame = -1;
    std::function<void(int id, const float pos[3], const float vel[3])> throw_item;   // the item leaves the hand
    std::function<void(int se)> play_se;               // sound effects of the player's actions
    static bool is_throw_item(int id);                 // CActiveItemPack::CheckStatusType == 1

private:
    void ungaga_key_on();
    void ungaga_key_play(bool ok_on, float dir_x, float dir_z);
    void osmond_key_on();
    void osmond_key_play(bool ok_on);
    void ruby_key_on();
    void ruby_key_play(bool ok_on);
    void ruby_shot(float scale, bool second);
    void quick_items(const PadRaw& pad, bool frozen);
    void drink_step();
    void throw_step();
    void throw_start(int slot);
    bool quick_item_useful(int slot) const;
    void quick_item_consume(int slot);
    void quick_item_apply(int id);
    uint32_t item_prev_buttons = 0;
    void xiao_key_on();
    void xiao_key_play(bool ok_on);
    void goro_key_on();
    void goro_key_play(bool ok_on, float dir_x, float dir_z);
    void toan_key_on();
    void toan_key_play(bool ok_on, float dir_x, float dir_z);
    void basic_damage(int sub_id, int owner_kind);
    void step_drive_counters();
    void update_weapon_tip();
    bool status_err(uint32_t mask) const;
    void set_status_err(int code);
    void add_now_life(float param, int ch, int16_t delta);
    const Dungeon* dungeon_;
    const FloorLayout* layout_;
    float far_limit_ = 80.0f;     // camera_far_dist_limmit
    float char_pos_[3] = {0, 0, 0};   // player position at the start of the tick (the CCharacter object's position)
    void auto_camera();
};

// Pieces exposed for tests and tools.
float unit_rotation(float target, float current);                 // turns toward the target by at most 15 degrees per tick
int key_ctrl(float lx, float ly, float* speed);                   // idle/run/walk choice and animation speed
void camera_step(CameraState& c, int ticks);                      // CCameraFollow::Step
void camera_base_step(CameraState& c);                            // CCamera::Step (one tick)
float angle_interpolate(float from, float to, float param, int mode);

}  // namespace dccore
