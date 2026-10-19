#include "dccore/combat.h"
#include "dccore/gamemath.h"
#include "dccore/monster_ai.h"

namespace dccore {

using namespace gm;

int ColPool::set(float radius, float scale_flag, const float pos[3], int damage, int life, int target, int kind, int flags, int extra) {
    for (int i = 0; i < kSize; ++i) {
        if (e[size_t(i)].used) continue;
        ColEntry& c = e[size_t(i)];
        c = ColEntry{};
        c.used = true;
        c.pos[0] = pos[0]; c.pos[1] = pos[1]; c.pos[2] = pos[2]; c.pos[3] = 1.0f;
        c.damage = damage;
        c.scale_flag = scale_flag;
        c.radius = radius;
        c.life = life;
        c.target = target;
        c.kind = kind;
        c.flags = flags;
        c.extra = extra;
        c.serial = ++serial_counter;
        last = i;
        if (log) log->push_back(SetLog{{pos[0], pos[1], pos[2]}, damage, radius, target, kind, life, source});
        return i;
    }
    return -1;
}

void ColPool::pack(const ColEntry& c, float o[kColPackFloats]) {
    o[0] = float(c.serial);
    for (int k = 0; k < 3; ++k) { o[1 + k] = c.pos[k]; o[4 + k] = c.dir[k]; o[21 + k] = c.kick_pos[k]; }
    o[7] = float(c.damage); o[8] = c.scale_flag; o[9] = c.radius; o[10] = c.radius_growth; o[11] = float(c.life); o[12] = float(c.target); o[13] = float(c.kind);
    o[14] = float(c.flags); o[15] = float(c.extra); o[16] = float(c.owner_kind); o[17] = float(c.owner_id); o[18] = float(c.sub_id); o[19] = float(c.status_flags);
    o[20] = float(c.only_monster_kind); o[24] = c.kick_speed; o[25] = c.kick_decay; o[26] = float(c.kick_mode); o[27] = float(c.hit_reset);
    o[28] = c.slayer ? 1.0f : 0.0f;
    for (int k = 0; k < 16; ++k) o[29 + k] = c.slayer ? float(c.slayer[k]) : 0.0f;
}

void ColPool::unpack(const float o[kColPackFloats], ColEntry& c) {
    c = ColEntry{};
    c.used = true;
    c.serial = uint32_t(o[0]);
    for (int k = 0; k < 3; ++k) { c.pos[k] = o[1 + k]; c.dir[k] = o[4 + k]; c.kick_pos[k] = o[21 + k]; }
    c.pos[3] = 1.0f; c.kick_pos[3] = 1.0f;
    c.damage = int32_t(o[7]); c.scale_flag = o[8]; c.radius = o[9]; c.radius_growth = o[10]; c.life = int32_t(o[11]); c.target = int32_t(o[12]); c.kind = int32_t(o[13]);
    c.flags = int32_t(o[14]); c.extra = int32_t(o[15]); c.owner_kind = int32_t(o[16]); c.owner_id = int32_t(o[17]); c.sub_id = int32_t(o[18]); c.status_flags = int32_t(o[19]);
    c.only_monster_kind = int32_t(o[20]); c.kick_speed = o[24]; c.kick_decay = o[25]; c.kick_mode = int32_t(o[26]); c.hit_reset = int32_t(o[27]);
    c.hit_counter = c.hit_reset;
    if (o[28] != 0.0f) {
        for (int k = 0; k < 16; ++k) c.slayer_store[size_t(k)] = int8_t(o[29 + k]);
        c.slayer = c.slayer_store.data();
    }
}

int ColPool::inject(const ColEntry& src) {
    for (int i = 0; i < kSize; ++i) {
        if (e[size_t(i)].used) continue;
        e[size_t(i)] = src;
        if (src.slayer) e[size_t(i)].slayer = e[size_t(i)].slayer_store.data();
        e[size_t(i)].used = true;
        return i;
    }
    return -1;
}

void ColPool::set_kickback(const float pos[3], float speed, float decay, int mode) {
    ColEntry& c = e[size_t(last)];
    c.kick_pos[0] = pos[0]; c.kick_pos[1] = pos[1]; c.kick_pos[2] = pos[2];
    c.kick_speed = speed;
    c.kick_decay = decay;
    c.kick_mode = mode;
}

void ColPool::step() {
    for (ColEntry& c : e) {
        if (!c.used) continue;
        if (c.hit_counter < 1) c.hit_counter = c.hit_reset;
        else --c.hit_counter;
        if (--c.life < 1) {
            c.used = false;
            c.scale_flag = 0.0f;
        } else {
            c.radius = fadd(c.radius, c.radius_growth);
        }
    }
}

int ColPool::check_hit_user(float height, const float pos[3], uint32_t mask) const {
    const float bottom = pos[1];
    const float top = fadd(bottom, height);
    const float flat[3] = {pos[0], 0.0f, pos[2]};   // the game zeroes the y lane of both points before measuring
    for (int i = 0; i < kSize; ++i) {
        const ColEntry& c = e[size_t(i)];
        if (!c.used || (mask & uint32_t(c.target)) == 0 || c.hit_counter != c.hit_reset) continue;
        const float eflat[3] = {c.pos[0], 0.0f, c.pos[2]};
        if (vu_dist(flat, eflat) > c.radius) continue;
        const float hi = fadd(c.pos[1], c.radius), lo = fsub(c.pos[1], c.radius);
        bool clear = true;
        if (top <= hi && lo < top) clear = false;
        if (bottom <= hi && lo < bottom) clear = false;
        if (hi <= top && bottom < lo) clear = false;
        if (top <= hi && lo < bottom) clear = false;
        if (!clear) return i;
    }
    return -1;
}

}  // namespace dccore
