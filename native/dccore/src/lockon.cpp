// The lock-on target selection of the dungeon (SetNearLockOnTarget @0x1dc0160, setTargetCursor @0x1dc07a0, LockOffTargte @0x1dbfce0), ported from the decompiled overlay.
#include <algorithm>
#include <cmath>
#include "dccore/gamemath.h"
#include "dccore/libm_ps2.h"
#include "dccore/monster_ai.h"
#include "dccore/vu0.h"

namespace dccore {

using namespace gm;

namespace {
constexpr float kLockScale[6] = {1.2f, 1.4f, 1.1f, 1.5f, 1.0f, 1.8f};   // how much farther than the monster's own lock-on distance each character reaches
}

void MonsterWorld::lock_off() {
    lock.target = -1;
    lock.flag = 0;
    lock.shift_no = -1;
    lock.shift_rot = 0;
}

int MonsterWorld::set_near_target(int shift, bool force) {
    const int ch = std::clamp(status.character(), 0, 5);
    const float scale = kLockScale[ch];
    const float s = ps2_sinf(player.yaw), c = ps2_cosf(player.yaw);
    const float fwd_raw[3] = {s, 0.0f, c};
    const vu0::V3 fwd = vu0::normalize(fwd_raw);
    const float me[3] = {player.pos[0], player.pos[1], player.pos[2]};
    int cand[16];
    float dist[16];
    int count = 0;
    for (int k = 0; k < 16; ++k) cand[k] = -1;
    for (int i = 0; i < MonsterUnit::kSlots; ++i) {
        const MonsterActor& a = actors[size_t(i)];
        if (a.slot < 0 || a.state != 2 || a.hp <= 0) continue;
        if (a.active == 0 || a.lockon_sw == 0) continue;
        const float p[3] = {a.pos[0], a.pos[1], a.pos[2]};
        const float d = vu0::dist(me, p);
        if (!(d < fmul(a.lockon_dist, scale))) continue;
        float v[3] = {fsub(p[0], me[0]), 0.0f, fsub(p[2], me[2])};
        const vu0::V3 n = vu0::normalize(v);
        const float dot = vu0::inner(fwd.data(), n.data());
        if (0.25f <= dot) {
            cand[count] = i;
            dist[count] = d;
            ++count;
        }
    }
    if (count == 0) {
        lock_off();
        return 0;
    }
    // the candidates sorted by distance (the game's selection sort: the nearest of what is left each round)
    int sorted[16];
    int left[16];
    float dleft[16];
    for (int k = 0; k < 16; ++k) sorted[k] = -1;
    for (int k = 0; k < count; ++k) { left[k] = cand[k]; dleft[k] = dist[k]; }
    for (int r = 0; r < count; ++r) {
        int first = 0;
        while (left[first] == -1) ++first;
        float best = dleft[first];
        int bi = first;
        for (int k = 0; k < count; ++k) {
            if (k != r && left[k] != -1 && dleft[k] < best) {
                best = dleft[k];
                bi = k;
            }
        }
        sorted[r] = left[bi];
        left[bi] = -1;
    }
    int pick = sorted[0];
    if (!force && count != 1) {
        int p1 = shift;
        if (count <= p1) p1 = 0;
        if (lock.target == sorted[p1]) ++p1;
        if (count <= p1) p1 = 0;
        pick = sorted[p1];
    }
    if (pick == -1) {
        lock_off();
        return 0;
    }
    if (lock.target != pick || force) {
        lock.cursor_cnt = 8.0f;
        lock.target = pick;
    }
    return count;
}

void MonsterWorld::update_lock_cursor() {
    if (lock.flag == 0) {
        lock.shift_rot = set_near_target(0, true);
        return;
    }
    if (lock.target < 0) return;
    const int ch = std::clamp(status.character(), 0, 5);
    const MonsterActor& a = actors[size_t(lock.target)];
    bool ok = true;
    if (a.state != 2 || a.hp < 1) ok = false;
    if (a.lockon_sw == 0) ok = false;
    const float me[3] = {player.pos[0], player.pos[1], player.pos[2]};
    float p[3] = {a.pos[0], a.pos[1], a.pos[2]};
    const float d = vu0::dist(me, p);
    if (fmul(a.lockon_dist, kLockScale[ch]) <= d) ok = false;
    if (a.kind != 2) {
        const float a2[3] = {me[0], 0.0f, me[2]}, b2[3] = {p[0], 0.0f, p[2]};
        const float d2 = vu0::dist(a2, b2);
        if (d2 <= fadd(a.body_radius, 8.0f) && fadd(p[1], 10.0f) < me[1]) ok = false;
        float dy = fsub(me[1], p[1]);
        if (dy < 0.0f) dy = -dy;
        if (80.0f <= dy) ok = false;
    }
    p[1] = fadd(p[1], 8.0f);
    if (!a.lock_bone.empty() && a.lock_joint >= 0) {
        p[0] = a.lock_pos[0]; p[1] = a.lock_pos[1]; p[2] = a.lock_pos[2];
    }
    lock.pos[0] = p[0]; lock.pos[1] = p[1]; lock.pos[2] = p[2]; lock.pos[3] = 1.0f;
    const int wid = status.weapon(status.character()).id();
    if ((wid == 0x12f || wid == 0x138) && lock.flag != 0 && rng_) {   // a joke of one weapon: a rand() every frame
        rng_->next();
    }
    if (ok) {
        if (0.0f < lock.cursor_cnt) lock.cursor_cnt = fsub(lock.cursor_cnt, 0.5f);
        else lock.cursor_cnt = 0.0f;
    } else if (8.0f <= lock.cursor_cnt) {
        lock.shift_no = 0;
        lock.shift_rot = 0;
        lock.flag = 0;
        lock.target = -1;
    } else {
        lock.cursor_cnt = fadd(lock.cursor_cnt, 0.25f);
    }
}

}  // namespace dccore
