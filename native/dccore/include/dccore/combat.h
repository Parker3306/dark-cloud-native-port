#pragma once
// The game's hit boxes (CCollisionData, "NowColData"): a pool of 96 spheres. Whoever attacks puts a sphere in the pool for a frame or two (a monster's attack
// frames, the player's weapon, shots); whoever can be hurt looks for a sphere that touches its body spheres. Ported from CCollisionData::Set / CheckHitUser /
// SetKickBack and the per-frame upkeep in motionDrive (see notes/native-plan.md, Milestone 4).
#include <array>
#include <cstdint>
#include <vector>

namespace dccore {

// One pool entry (0xa0 bytes in the game). Field names describe what the game does with them.
struct ColEntry {
    bool used = false;
    float pos[4] = {0, 0, 0, 1};      // +0x00 centre of the sphere
    float dir[4] = {1, 0, 0, 0};      // +0x20 knock-back direction given to a player that is hit (kind 3)
    int32_t damage = 0;               // +0x34
    float scale_flag = 0;             // +0x38 non-zero: the direction is rescaled every frame
    float radius = 0;                 // +0x3c
    float radius_growth = 0;          // +0x40 added to the radius every frame
    int32_t life = 0;                 // +0x44 frames left (the entry is freed when it reaches 0)
    int32_t target = 0;               // +0x48 bit mask of who can be hit: 1 the player, 2 monsters (3 stays after a hit)
    int32_t kind = 0;                 // +0x4c 3 = hit throws the player back
    int32_t flags = 0;                // +0x50 element (1 2 4 8 0x10) and effect bits (0x40000 steal gold, 0x1000 ...)
    int32_t extra = 0;                // +0x54
    int32_t owner_kind = -1;          // +0x58 0..5: a player character, 200 + 5 * slot: a monster, -1: none
    int32_t owner_id = -1;            // +0x5c
    int32_t sub_id = -1;              // +0x60 which of the owner's attacks (combo step / hit box number)
    const int8_t* slayer = nullptr;   // +0x64 weapon's table of bonuses against kinds of monsters (percent / 1.5 per point)
    int32_t status_flags = 1;         // +0x6c weapon status effects
    int32_t only_monster_kind = -1;   // +0x68 only hurts this kind of monster
    float kick_pos[4] = {0, 0, 0, 1}; // +0x80 where a knock-back comes from
    float kick_speed = 0;             // +0x90
    float kick_decay = 0;             // +0x94
    int32_t kick_mode = 0;            // +0x98 2 = pushes monsters away
    int32_t hit_counter = 0;          // +0x70 counts down; the entry can hit while it equals +0x74
    int32_t hit_reset = 0;            // +0x74
    // co-op (not in the game)
    int32_t remote = 0;               // host: the remote player whose sphere this is (0: the host's own player or a monster)
    uint32_t serial = 0;              // counts the spheres of a pool; names a sphere on the wire
    std::array<int8_t, 16> slayer_store{};   // a copy of the weapon's slayer table for spheres that came over the wire (slayer then points here)
};

constexpr int kColPackFloats = 45;    // size of a sphere on the wire (ColPool::pack)

// A record of one Set() call, for the comparison rigs.
struct SetLog {
    float pos[3];
    int damage;
    float radius;
    int target, kind, life;
    int source = 0;   // 1: put there by a shot effect
};

class ColPool {
public:
    std::vector<SetLog>* log = nullptr;   // when set, every successful Set() is appended
    int source = 0;                       // tag copied into the log (1 while the shot effects step)
    static constexpr int kSize = 0x60;
    std::array<ColEntry, kSize> e;
    int last = 0;                     // +0x3d80: index of the entry Set() filled last
    uint32_t serial_counter = 0;      // co-op: the serial of the next sphere
    // co-op: a sphere as floats (and back); inject puts a sphere that came over the wire into a free entry (-1 when the pool is full)
    static void pack(const ColEntry& c, float out[kColPackFloats]);
    static void unpack(const float in[kColPackFloats], ColEntry& c);
    int inject(const ColEntry& src);

    // CCollisionData::Set: fills the first free entry (does nothing when all 96 are used) and returns its index (or -1).
    int set(float radius, float scale_flag, const float pos[3], int damage, int life, int target, int kind, int flags, int extra);
    void set_kickback(const float pos[3], float speed, float decay, int mode);
    // motionDrive's upkeep: cooldowns, lifetimes, radius growth
    void step();
    // CCollisionData::CheckHitUser: first entry whose target mask shares a bit with `mask`, that is ready to hit, whose sphere reaches the point horizontally and
    // overlaps the vertical span [y, y + height]; -1 if none.
    int check_hit_user(float height, const float pos[3], uint32_t mask) const;
};

}  // namespace dccore
