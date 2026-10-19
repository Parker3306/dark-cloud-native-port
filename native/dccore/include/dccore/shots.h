#pragma once
// Shot effects (CSHOT_EFFECT): the projectiles and zones of the monsters' ranged attacks (and, later, the characters' magic and slingshots). A definition row of
// BtEntryEffectTbl (main executable) names an effect model (dun/effect/<name>.chr) whose motions play the phases of the shot: 0 start, 1 flight, 2 hit, 3 end of
// range; every frame a flying shot looks for a wall, the player or a monster on its way and puts a damage sphere into the hit box pool.
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "dccore/character.h"
#include "dccore/combat.h"
#include "dccore/disc.h"
#include "dccore/elf.h"
#include "dccore/motion_state.h"
#include "dccore/gamemath.h"

namespace dccore {

struct GameRand;

// BT_SHOT_EFFECT (0x70 bytes)
struct ShotDef {
    std::string name;            // +0x00: effect model dun/effect/<name>.chr
    int mode = 0;                // +0x10: 0 flies along the line from muzzle to target; 2 stays where it was set
    int look_at = 0;             // +0x14: the model is turned along the flight direction
    float speed[4] = {};         // +0x18: units per tick in each phase
    float radius[4] = {};        // +0x28: radius of the damage sphere in each phase (<= 0: none)
    int life = 0;                // +0x38: range in ticks of flight
    int damage = 0;              // +0x3c
    int attr = 0;                // +0x40: effect bits of the damage sphere (element / status)
    int kind = 0;                // +0x44: 3 throws the player back
    int target = 0;              // +0x48: who is hit: 1 the player, 2 monsters
    int16_t motion[4] = {-1, -1, -1, -1};   // +0x4c: effect model motion played in each phase (-1 none)
    int type = 0;                // +0x54: 100 = a bomb explosion follows the shot
    float bomb_scale = 0;        // +0x58
    int bomb_param = 0;          // +0x5c
    int sound[4] = {-1, -1, -1, -1};   // +0x60
    static ShotDef from_elf(const Elf& elf, uint32_t addr);
    // The characters' own effect rows live in the dungeon overlay (DUN.BIN, loaded at 0x01DABD00): `row` points at the 0x70 bytes of one definition.
    static ShotDef from_memory(const uint8_t* row);
    static constexpr uint32_t kOverlayBase = 0x01DABD00;
};

class ShotEffect {
public:
    static constexpr int kInstances = 8;
    struct Instance {
        bool active = false;
        int phase = 0;                  // 0 start, 1 flight, 2 hit, 3 end of range
        float pos[4] = {0, 0, 0, 1};
        float vel[4] = {0, 0, 0, 0};
        MotionState ms;
        MotionRequest rq;
        int life_counter = -1;          // ticks of flight left
        int damage = 0;
        int sub_id = -1;
        int owner_kind = -1;
        int user_id = -1;               // the monster that fired it
        int cooldown = -1;              // 0x283c: -1 for ever, otherwise ticks of damage left (SetLifeTime-like)
        int status_flags = 1;           // 0x280c: weapon status effects of the damage sphere (SetWepStatus)
        const int8_t* slayer = nullptr; // 0x2824: the weapon's bonus table against kinds of monsters (SetVsMonster)
        int wait = 0, wait_counter = 0;   // SetWait: ticks between two damage spheres
        int loop = -1;                  // 0xa0b0: SetLoop: phases the effect cycles through without hitting anything (-1: a normal shot)
        float random_rate = -1.0f;      // 0x2834: >= 0: the effect jitters when drawn (three rand() draws per tick)
        bool no_sound = false;
        bool attach = false;            // hangs on the character instead of flying (the front end draws it at the weapon frame)
    };
    ShotDef def;
    std::shared_ptr<Character> model;
    std::vector<MotionInfo> table;
    std::array<Instance, kInstances> inst;
    bool loaded = false;
    int def_index = -1;                 // row of BtEntryEffectTbl this slot plays

    // CSHOT_EFFECT::Set: starts an instance at `muzzle` heading for `target`; returns its index or -1.
    int set(const float muzzle[3], const float target[3], int owner_kind, int sub_id, int force_phase = -1);
    // EndEffect: every shot still starting or flying goes into its hit phase.
    void end_all();
    void off(int index);   // OffEffect: -1 stops every instance
};

struct ShotEnv {
    ColPool* pool = nullptr;
    // checkCollision(radius, pos, vel, target, hit): 0 nothing, 1 wall, 2 the player, 3 a monster
    std::function<int(float radius, const float pos[3], const float vel[3], int target, float hit[3])> collide;
    const float* player_pos = nullptr;
    // a shot with type 100 ends in an explosion: scale, centre, who is hurt, damage
    std::function<void(float scale, const float pos[3], int target, int damage)> on_bomb;
};

class ShotSystem {
public:
    static constexpr int kSlots = 5;
    std::array<ShotEffect, kSlots> slot;

    // The rows of BtEntryEffectTbl (main executable).
    static std::vector<ShotDef> load_table(const Elf& elf);
    // CSHOT_EFFECT_PACK::Entry: the slot that plays definition `def_index` (taking the loaded model the first time), -1 when there is none.
    int entry(int def_index, const ShotDef& def, std::shared_ptr<Character> model);
    // Step of every slot (logic), then the draw-time motion step of the instance objects.
    enum class Which { All, Only, Except };
    // `which` Only / Except restricts the step to (or leaves out) the slot `slot_index`: the character's own effect steps with the update phase, the monsters' with the effects.
    void step(ShotEnv& env, Which which = Which::All, int slot_index = -1);
    void step_motions(GameRand* rng = nullptr);   // `rng`: the draws of jittering effects
    int active_count() const;
};

}  // namespace dccore
