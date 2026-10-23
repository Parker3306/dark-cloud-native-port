#pragma once
// Monster behaviour: the per-tick logic of CMonstorUnit (Step, CheckViewLevel, MoveCheck2, MoveChecMonster) and the external functions that the monsters'
// `.stb` scripts call (_GET_POSITION, _SET_MOTION, _SET_MOVE, ...), plus the motion-time part of CCharacter::Step that the scripts' timing depends on
// (_CHK_MOTION_FRM). Ported from the decompiled dungeon overlay (see notes/native-plan.md, Milestone 4).
//
// Damage (CheckDmg), shots, hit / body collision registration, sounds and item drops are in monster_combat.cpp / shots.cpp. Not ported: the palette effects of
// monsters (poison / power-up tints) and loop sounds.
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "dccore/collision.h"
#include "dccore/combat.h"
#include "dccore/coop_scale.h"
#include "dccore/lockon.h"
#include "dccore/shots.h"
#include "dccore/dungeon.h"
#include "dccore/floor.h"
#include "dccore/monsters.h"
#include "dccore/motion_state.h"
#include "dccore/script.h"
#include "dccore/drops.h"
#include "dccore/items.h"
#include "dccore/user_status.h"

namespace dccore {

// The game's vector helpers (VU0 code: sums in a fixed order, round toward zero).
float vu_dist(const float a[3], const float b[3]);                 // DistVector
void vu_normalize(const float in[3], float out[3]);                 // sceVu0Normalize (xyz only)

// A sphere that hurts when something attacking touches it (SET_BODY_COL): the monster's body. `bone` is the MDS object it follows.
struct BodyCol {
    bool active = false;
    std::string bone;
    int joint = -1;
    float radius = 0;
    float win_start = 0, win_end = 0;       // when non-zero, only active between these motion frames
    float pos[4] = {0, 0, 0, 1};            // world position (updated when the monster is drawn)
    std::array<int32_t, 5> element_pct{100, 100, 100, 100, 100};
    std::array<int32_t, 6> attacker_pct{100, 100, 100, 100, 100, 100};   // damage percent for each playable character's attacks
};

// A sphere the player's movement bumps into (SET_MOV_COL): up to 12 per monster, following a bone.
struct MovCol {
    bool active = false;
    std::string bone;
    int joint = -1;
    float radius = 0;
    float pos[4] = {0, 0, 0, 1};
};

// An attack sphere (SET_DMG_COL / SET_DMG_PARA): active between two motion frames, it puts an entry in the hit box pool every frame.
struct DmgCol {
    bool active = false;
    std::string bone;
    int joint = -1;
    float radius = 0, win_start = 0, win_end = 0;
    int32_t damage = 0, flags = 0, type = 0;
    float angle = 0;                         // type 3: knock-back direction relative to the monster's facing, 0 = away from the monster
    float pos[4] = {0, 0, 0, 1};
};

// Things that happened that the rest of the game (sound, effects, HUD, items) will want to show.
struct CombatEvent {
    enum Kind { Hit, Blocked, Poison, Death, DropGold, DropItem, Steal, WeaponExp, WeaponBroke, WeaponRepaired, WeaponWarn } kind = Hit;
    int slot = -1;
    float pos[3] = {0, 0, 0};
    int value = 0, value2 = 0;
    bool processed = false;                   // step_effects() has dealt with it
    bool processed_notice = false;
    int remote = 0;                           // co-op host: the remote player this event belongs to (0: the host's own player)
};

// A shot the script asked for with _SET_SHOT / _SET_SHOT2: the muzzle is a named frame of the model; the shot starts the next time the unit steps.
struct ShotRegistration {
    int state = 0;                            // 0 idle, 1 registered (the frame's position is read when the monster is drawn), 2 ready to fire
    std::string bone;
    int joint = -1;
    float target[4] = {0, 0, 0, 1};           // where the shot is aimed (a world position given by the script)
    float muzzle[4] = {0, 0, 0, 1};           // world position of the frame, read at the drawn frame
    int damage = -1;                          // overrides the effect's damage when >= 0
};

// Everything about one monster that the scripts and the unit logic read or write: the 400-byte "unit info" record plus the CCharacter's transform and motion.
struct MonsterActor {
    int slot = -1, base = -1;
    // CCharacter
    float pos[3] = {0, 0, 0};
    float rot[3] = {0, 0, 0};                 // rot[1] = yaw about Y (0 = facing +Z)
    MotionRequest request;
    MotionState motion;
    std::vector<MotionInfo> table;
    int motion_phase = 0;                     // CCharacter +0xc70: 1 changed, 2 playing, 3 on the last frame
    // unit info
    int state = -1;                           // -1 empty/dead, 1 waiting (far away), 2 active
    int owner = -1;                           // last attacker's character
    int hit_stop = 0;                         // frames of hit pause
    int status_slow = 0;                      // 0x1e3e4: > 0 halves script speeds
    int bin2 = 0;                             // 0x1e3e0 (_SET_BIN2 / _GET_STATUS_BIN2)
    struct SndFrame { float start = 0, end = 0; int id = -1; int cool = 0; };   // _SET_SND_FRM: a sound that plays while the motion is in [start, end) (0x5ee50 / 0x5ee90 / 0x5eed0 / 0x5ef10)
    std::array<SndFrame, 16> snd_frames;
    bool kind_boss = false;                   // the row kind is 2: heard from further away
    float dist_to_player = 0;
    int hp = 0, hp_max = 0;
    float body_radius = 13.0f;                // 0x1e414: wall radius
    float mon_radius = 13.0f;                 // 0x1e418: radius against other monsters
    float move_dir[4] = {0, 0, 0, 0};         // 0x1e430
    float face_target[4] = {0, 0, 0, 0};      // 0x1e440
    float move_speed = 0;                     // 0x1e450
    float rot_speed = 0;                      // 0x1e454
    int fall = 1;                             // 0x1e458: gravity / floor-glue
    int muteki = 0;                           // 0x1e468 invincibility counter
    float clip_level = 300.0f;                // 0x1e474: distance at which the monster wakes up
    int col_off = 0;                          // 0x1e478: collisions off counter
    float height = 0;                         // 0x1e488: height above the floor
    float floor_y = 0;                        // 0x1e48c
    int hit_vol = -1;                         // 0x1e490 (_STATUS_GET_HITDMG_VOL)
    int dmg_id = 0;                           // 0x1e494
    std::string lock_bone;                    // 0x1e4cc (_STATUS_SET_LOCKON_TRG): the frame the lock-on cursor sits on (empty: the monster's position + 8 up)
    int lock_joint = -1;
    float lock_pos[4] = {0, 0, 0, 1};         // 0x1e4d0: that frame's world position at the drawn frame
    float lockon_dist = 120.0f;               // 0x1e4e8
    int lockon_sw = 1;                        // 0x1e4ec
    int16_t active = -1;                      // 0x1e4a4: 0 = hidden/disabled, 1 = revive, otherwise normal
    int16_t gravity_mode = 0;                 // 0x1e4a6 (_SET_GRAVITY)
    int16_t kind = 0;                         // 0x1e410 (entry +0x78)
    float push_dir[4] = {0, 0, 0, 1};         // 0x1e540 knock-back
    float push_speed = 0, push_decay = 0;     // 0x1e550 / 0x1e554
    float alpha = 128.0f;                     // 0x1e4f0 (opacity, 0 = gone, fades by alpha_rate every frame once alpha_wait is used up)
    float alpha_rate = 0.0f;                  // 0x1e4f4
    int alpha_wait = 0;                       // 0x1e4f8
    int last_motion_id = 0;                   // 0x1e4bc: the last motion a script asked for (restored after a pause)
    int last_motion_flags = 0;                // 0x1e4be
    float last_motion_speed = -1.0f;          // 0x1e4c0
    // combat
    std::array<BodyCol, 16> body_cols;
    std::array<DmgCol, 16> dmg_cols;
    std::array<MovCol, 12> mov_cols;
    int cur_dmg_col = -1;                     // the attack sphere SET_DMG_PARA applies to (0x5a690)
    int poison = 0;                           // 0x1e3dc: counts down; every 180 frames 10% of the life is lost
    int monster_code = 0;                     // 0x1e3f8 (entry +0x54): kind of monster (slayer table index)
    std::array<int16_t, 5> resist{};          // 0x1e3fa (entry +0x56): percent damage taken from fire, ice, thunder, wind, holy
    int16_t defense = 0;                      // 0x1e460 (entry +0x64)
    int16_t weapon_wear = 0;                  // 0x1e462 (entry +0x66)
    int32_t gold = 0, drop_pct = 0;           // 0x1e404 / 0x1e408 (entry +0x70 / +0x74)
    int32_t stolen_gold = 0;                  // 0x1e40c
    int32_t exp = 0;                          // 0x1e480 (entry +0x6c): weapon experience given
    int16_t steal_item = -1;                  // 0x1e4a8 (entry +0x80)
    int16_t drops_enabled = 0;                // 0x1e4aa (entry +0x82)
    int16_t dmg_scale = 100;                  // 0x1e4ac (entry +0x84): percent of damage taken from non-player sources
    int16_t status_chance = 0;                // 0x1e4ae (entry +0x86): chance (percent) that status effects work
    int16_t rare_item = -1;                   // 0x1e4b0 (entry +0x88)
    int16_t dropped_item = -1;                // 0x1e470
    std::array<ShotRegistration, 2> shot;     // 0x5ff50 / 0x60250
    std::array<int, 2> shot_slot = {-1, -1};  // 0x1e47c / 0x1e47e: the ShotSystem slot of each shot effect (-1 none)
    int killer = -1;                          // 0x1e3d4: which character killed it
    int element_hit = 0;                      // 0x1e498
    int hit_body_col = 0;                     // CMonstorUnit +0x55750 + slot*0x510
    int hit_status = 0;                       // +0x55754: status flags of the weapon that hit last
    float push_scale = 1.0f;                  // 0x1e558 (entry +0x98)
    int16_t death_event = -1;                 // 0x1e4c6
    int16_t death_event_pending = 0;          // 0x1e4c8
    bool motion_restore = false;              // 0x1e4c4: put the last motion back (after a hit pause)
    std::array<int16_t, 3> guard_on{};        // SET_GUARD_FRAME records (0x60550)
    std::array<float, 3> guard_start{}, guard_end{};
    // script
    ScriptVm vm;
    bool script_active = false;               // CMonstorUnit +0x50 + 4*slot: a script function is mid-run
    std::vector<std::string> events;          // ignored script calls, for tracing: "name args"
};

struct PlayerInfo {
    float pos[3] = {0, 0, 0};                 // where the player is (as of the end of MoveChara)
    float prev_pos[3] = {0, 0, 0};            // 0x1ea1d30: the player's position as the monsters' Step sees it (set in motionDrive after their Step: one frame behind)
    int attack_idle = 0xe10;                  // DAT_01dc4560: frames since the player last attacked (GUARD_SEARCH reports it and may reset it)
    float yaw = 0;
    int character = 0;                        // UserStatus +4
    float facing[4] = {0, 0, 1, 0};           // DAT_01dc4540: in fact the stick direction rotated by the camera (x, 0, z, 1)
    float move_vec[3] = {0, 0, 0};            // DAT_01dc4550
};

// Co-op (host side): another player's record and place. The monsters look for the nearest player; when that player's sphere hits a monster the damage code runs with
// his record and position swapped in (RemoteScope in monster_combat.cpp) and whatever it changed in the record is reported as patches for his own game.
struct RemotePlayer {
    UserStatus status;                        // the host's copy of the record (the player sends the whole record now and then)
    PlayerInfo info;
};

struct StatusPatch {
    int remote = 0;
    uint32_t offset = 0;
    std::vector<uint8_t> bytes;
};

class MonsterWorld {
public:
    MonsterWorld(const Dungeon* dungeon, const FloorLayout* layout, const MonsterUnit* unit, GameRand* rng);

    std::array<MonsterActor, MonsterUnit::kSlots> actors;
    PlayerInfo player;
    ColPool pool;                             // NowColData
    UserStatus status;                        // the player's record (weapons, life, items)
    std::vector<CombatEvent> events;          // filled by combat code, to be drained by the caller
    int alive = 0;                            // CMonstorUnit +0x4c: number of monsters alive
    int floor_map = 0;                        // selectMapNo: which dungeon the floor belongs to
    int last_attacker = -1;                   // DAT_01dc4528
    bool double_weapon_exp = false;           // CMonstorUnit +0x44
    int coop_players = 1;                     // co-op balance (coop_scale.h): the players of this floor; monsters spawned from now on get the hit points, damage and drops of that party
    void set_coop_players(int n) {
        coop_players = coop_clamp_players(n);
        pool.monster_damage_mult = coop_damage_mult(coop_players);
    }
    std::array<int32_t, 32> gate_key_stack = [] { std::array<int32_t, 32> v; v.fill(-1); return v; }();   // gate keys already dropped on this floor
    static constexpr std::array<int16_t, 6> kDefaultWeapon{257, 299, 314, 331, 347, 363};   // defWeapon: the weapon each character starts with
    int bak_body_col = -1;                    // bak_ColNo: the body sphere SET_BODY_COL_PARA applies to
    std::array<int32_t, 512> globals{};       // GL_INT: integers shared between the scripts
    bool coop_client = false;                 // co-op client: monsters, drops and their effects are the host's (snapshots); only the local player's own effects run
    std::vector<std::unique_ptr<RemotePlayer>> remotes;   // co-op: id = index + 1 (the slot of a removed player stays empty)
    std::vector<StatusPatch> status_patches;              // co-op: changes made to the remote records since the front end last took them
    int add_remote(const UserStatus& status) {
        auto r = std::make_unique<RemotePlayer>();
        r->status = status;
        for (size_t i = 0; i < remotes.size(); ++i)
            if (!remotes[i]) { remotes[i] = std::move(r); return int(i) + 1; }
        remotes.push_back(std::move(r));
        return int(remotes.size());
    }
    RemotePlayer* remote(int id) { return id >= 1 && size_t(id) <= remotes.size() ? remotes[size_t(id - 1)].get() : nullptr; }
    void remove_remote(int id) { if (id >= 1 && size_t(id) <= remotes.size()) remotes[size_t(id - 1)].reset(); }
    bool paused = false;                      // CMonstorUnit +0x98: the last Step was a hold frame
    int hold = 0;                             // driveStepHold | CMonUnitHold of the frame about to be stepped
    bool last_floor = false;                  // the floor's monsters are never put back to sleep on the last floor of a dungeon
    bool trace = false;
    bool trace_ext = false;                   // fill ext_log with every script call (slot, id, raw argument slots)
    std::vector<std::string> ext_log;
    int rotation_mode = 0;                    // AngleInterpolate's mode for turning monsters (the call site leaves it unspecified)
    std::vector<std::string> log;
    float boss_hover_cap = 0.0f;              // boss floors (0 = off): the boss (slot 0) is kept at or below this height, so a sword (reach about 30) can hurt it
    bool boss_free_flight = false;            // boss floors: the boss (slot 0) flies through the arena's pillars (its script heads straight for the arena centre)

    // The polygons that stand in for a monster when another monster looks for obstacles (dun/etc/atrcol.mds: a prism scaled by 0.2 * radius).
    void set_monster_shape(CollisionPiece shape) { shape_ = std::move(shape); }

    int run_script_request = -1;              // _RUN_SCRIPT / _STATUS_SET_EVENT of a monster (a boss's death: event 510): the front end takes it
    int run_script_slot = -1;
    // SetupViewMonstor: puts monster `slot` (already placed in unit->slots) into the world and runs its init function.
    void spawn(int slot);
    void spawn_all();
    // CleanViewMonstor: every monster leaves the world (the back-floor swap reloads the one unit); the player's own effect stays.
    void clear_monsters();
    void trap_bomb(const float pos[3]);   // _BOM_SET: a bomb goes off at the player (SetBombEffect, damage 20 per dungeon number)
    void trap_hurt(const float pos[3]);   // _SET_STATUS_ERR: the small damage sphere that goes with a status trap
    // One game tick of CMonstorUnit::Step for every monster (CheckViewLevel, scripts, movement) followed by the motion step of CCharacter::Step.
    void tick() { step_ai(); step_motions(); }
    // The two halves of a game frame: the unit's Step (scripts, movement) runs in the update, the motion step in the draw that follows it.
    void step_ai();
    void step_motions();

    // exposed for tests
    void check_view_level();
    void step_motion(MonsterActor& a);
    // CheckDmg: attack spheres of the monster go into the pool; the player's attacks in the pool hurt it. Returns 0 nothing, 1 flinch (script 110), 2 died (script 120).
    int check_dmg(MonsterActor& a);
    void update_cols(MonsterActor& a, const PoseSample& pose);   // world positions of the body and attack spheres at the drawn frame
    void drop_loot(MonsterActor& a);
    void drop_loot_base(MonsterActor& a);   // the game's own loot rules (drop_loot adds the co-op bonus)
    int check_item_get(int item) const;
    int select_attach(const MonsterActor& a);
    // CMonstorUnit::SetKey (end of ArrangementPos): one random monster (three on dungeon 2) carries this dungeon's gate key unless the player has it already.
    void set_key(int map_no, int floor, int map1_variant);
    bool set_gate_key(int item);                      // what happens when a monster that died is cleaned up (weapon experience, gold, items)

    // CMonstorUnit::MoveCheck: the player's step `vel` stops (and he is pressed down by 2) when it runs into an awake monster.
    void move_check_player(const float pos[3], float vel[3], int character, bool lock_on);
    // The part of the frame that runs after everything has moved: drops appear, gold and items are picked up, weapon experience is added (FUN_01db8300, WeaponExp code of CheckDmg).
    void step_effects();
    Drops drops;
    // The characters' main weapon effect (NowMainEffect): the slot of `shots` that plays it (-1: none loaded), and the calls the attack code makes on it.
    int player_fx = -1;
    // The lock-on (SetNearLockOnTarget / setTargetCursor / LockOffTargte)
    LockState lock;
    int set_near_target(int shift, bool force);   // picks the monster to lock on to among those in front of the player; returns how many are in reach
    void lock_off();
    void update_lock_cursor();
    void lock_aim_pos(const MonsterActor& a, float out[3]) const;   // the lock-on cursor / aim point of a monster                    // the draw part: auto target while not locked; validates the lock otherwise and aims
    void sword_dmg_check(float param, int wear);   // BattleSubWeaponDmg / SwordDmgCheck1: the weapon wears down
    void load_player_fx(const ShotDef& def, std::shared_ptr<Character> model, int index);
    int fire_player_shot(const float pos[3], const float target[3], int owner_kind, int sub_id, int damage, int wait, int force_phase = -1, bool set_attribute = true, bool set_damage = true,
                         int loop = -1, float random_rate = -1.0f, bool no_sound = false, bool attach = false);
    void off_player_effect(int instance) { if (player_fx >= 0) shots.slot[size_t(player_fx)].off(instance); }
    void end_player_shot();
    void step_player_shots();
    void step_pool() { pool.step(); }   // motionDrive: the hit boxes age after the monsters' Step (before the effects)
    // The slingshot bullets (CSHOT: 12 of them): fly in a straight line, a sphere of damage where they hit a monster or a wall
    struct SlingBullet {
        bool active = false;
        float pos[4] = {0, 0, 0, 1}, vel[4] = {0, 0, 0, 0};
        int life = 0, damage = 0;
    };
    std::array<SlingBullet, 12> bullets;
    void fire_sling(const float pos[3], const float vel[3], int damage);
    void step_sling();
    // Osmond's machine gun (CSHOT_MACHINGUN: 16 bullets, 240 ticks of life, 4-radius spheres on monsters) and flame thrower (CSHOT_FIREBAR: 24 flame blobs)
    struct MgBullet {
        int state = 0;
        float pos[4] = {0, 0, 0, 1}, vel[4] = {0, 0, 0, 0};
        int damage = 0;
    };
    std::array<MgBullet, 16> mg;
    void fire_mg(const float pos[3], const float vel[3], int damage);
    void step_mg();
    struct FlameBlob {
        int state = -1;                 // -1 off, 0 burning
        float pos[4] = {0, 0, 0, 1}, vel[4] = {0, 0, 0, 0};
        float size = 0, life = 0;
        int damage = 0, element = 0;
    };
    std::array<FlameBlob, 24> flame;
    int flame_cnt = 0;                  // msg_cnt_1151: a sphere of damage every 30 ticks
    void firebar_init(const float pos[3], const float dir[3]);
    void firebar_set(const float pos[3], const float dir[3], int damage, int element);
    void firebar_reset();
    void step_firebar();
    ShotEnv shot_env();
    // Thrown items (CMainItemModel::Step / ItemThrowStep): up to 16 in flight; every one explodes 45 ticks after it left the hand or on the first monster it touches
    struct ThrownItem {
        bool active = false;
        int id = 0;
        float pos[3] = {0, 0, 0}, vel[3] = {0, 0, 0};
        int n = 0;
    };
    std::array<ThrownItem, 16> thrown;
    struct ItemBlast {   // for the front end: an item exploded here (sound, picture)
        int id;
        float pos[3];
    };
    std::vector<ItemBlast> blasts;
    int throw_item(int id, const float pos[3], const float vel[3]);
    struct SoundEvent {   // for the front end: a monster makes a sound (SndSePlay with a position)
        int id;
        float pos[3];
        bool boss;        // bosses are heard from further away
        bool stop;        // _STOP_SND_NOW
    };
    std::vector<SoundEvent> sounds;
    void sound_check(MonsterActor& a);   // CMonstorUnit::SoundCheck: the frame sounds of the motion
    void step_thrown();
    ShotSystem shots;                         // ShotEffect[5]
    void step_shot_motions() { shots.step_motions(rng_); }   // the draw-time part: the effect objects' motions advance
    const ItemDb* item_db = nullptr;

private:
    const Dungeon* dungeon_;
    const FloorLayout* layout_;
    const MonsterUnit* unit_;
    GameRand* rng_;
    int cur_ = 0;                             // NowMonstorUnit->+0x90: the monster whose script is running
    std::vector<CCPoly> polys_;
    CollisionPiece shape_;
    int terrain_count_ = 0;                   // polys_[0 .. terrain_count_) come from the floor, the rest from other monsters

    bool ext(int id, const ScriptArgs& a);
    void step_actor(MonsterActor& a);
    void move_check2(MonsterActor& a);
    void hit_effect_draws(int element);
    void move_check_monster(MonsterActor& a);
    void gather_polys(const MonsterActor& a, bool with_monsters);
    int shot_collide(float radius, const float pos[3], const float vel[3], int target, float hit[3]);   // checkCollision
    void fire_shots(MonsterActor& a);
};

}  // namespace dccore
