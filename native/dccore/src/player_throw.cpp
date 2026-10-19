// Throwing an item (bombs, stones, gems, fruit): MoveChara's start of the throw and BattleActionThlow (aiming with the reticle, the throw motion, the release).
// The flight and the explosion of the item are the monsters' world's business (MonsterWorld::throw_item / step_thrown). See notes/dungeon_items.md section 1.
#include <algorithm>
#include <cmath>
#include "dccore/gamemath.h"
#include "dccore/libm_ps2.h"
#include "dccore/walk.h"

namespace dccore {

// CActiveItemPack::CheckStatusType == 1: 0x98 Sacred Water, 0x9f Bomb, 0xa0 Stone, 0xa1..0xa5 the gems, 0xa6 Throbbing Cherry, 0xa7 Gooey Peach, 0xa8 Bomb Nuts, 0xa9 Poisonous Apple.
bool Walker::is_throw_item(int id) { return id == 0x98 || (id >= 0x9f && id <= 0xa9); }

// The use button on a throwable item (MoveChara ~177640): without a lock-on the reticle appears 31 units in front, with one the item is thrown at the target at once.
void Walker::throw_start(int slot) {
    (void)slot;
    PlayerCombat& c = combat;
    c.mode = 2;
    c.can_act = 0;
    c.held = 1;
    const bool locked = lock && lock->flag != 0;
    if (!locked) {
        throw_aim = 1;
        throw_state = -1;
        const float dx = ps2_sinf(player.yaw), dz = ps2_cosf(player.yaw);
        reticle[0] = gm::fadd(gm::fadd(dx, player.pos[0]), gm::fmul(dx, 30.0f));
        reticle[1] = player.pos[1];
        reticle[2] = gm::fadd(gm::fadd(dz, player.pos[2]), gm::fmul(dz, 30.0f));
        reticle[3] = 1.0f;
        c.wish = 0x1a;
    } else {
        throw_aim = 0;
        throw_state = 1;
        c.wish = 0x1b;
    }
}

namespace {
float length3(const float v[3]) {
    return gm::to_float(std::sqrt(double(gm::fadd(gm::fmul(v[2], v[2]), gm::fadd(gm::fmul(v[0], v[0]), gm::fmul(v[1], v[1]))))));
}
}  // namespace

// BattleActionThlow
void Walker::throw_step() {
    PlayerCombat& c = combat;
    if (c.mode != 2) {   // hit (or dead) while aiming: the throw is over and the item was not used
        throw_aim = 0;
        throw_state = -1;
        return;
    }
    const float t = motion.time;
    if (throw_aim != 0 && throw_state == -1) {
        if ((item_prev_buttons & kPadUse) == 0) {   // the button was let go: throw
            throw_aim = 0;
            throw_state = 1;
            c.wish = 0x1b;
        } else {
            const float cand[3] = {float(double(reticle[0]) + double(c.stick_dir[0])), reticle[1], float(double(reticle[2]) + double(c.stick_dir[1]))};
            const float d3[3] = {gm::fsub(cand[0], player.pos[0]), gm::fsub(cand[1], player.pos[1]), gm::fsub(cand[2], player.pos[2])};
            const float d = length3(d3);
            if (30.0f <= d && d <= 120.0f) {
                reticle[0] = cand[0];
                reticle[2] = cand[2];
            }
            player.yaw = unit_rotation(ps2_atan2f(d3[0], d3[2]), player.yaw);
            c.wish = 0x1a;
        }
        return;
    }
    const float end = float(motion_table[0x1b].end);
    if (throw_state == 1) {
        c.wish = 0x1b;
        static constexpr float kRelease[6] = {548.0f, 452.0f, 267.0f, 347.0f, 188.0f, 321.0f};
        const int ch = status->character();
        if (kRelease[std::clamp(ch, 0, 5)] <= t && t <= end) {
            const int slot = active_sel - 1;
            const int id = status->get<int16_t>(0x4362 + size_t(slot) * 2);
            if (play_se) play_se(0x96);
            quick_item_consume(slot);
            const float S[3] = {item_origin[0], item_origin[1], item_origin[2]};
            float v[3];
            if (!(lock && lock->flag != 0)) {   // ParabolicInitialVector(g 0.12, T 20): the game's own sign quirk is kept (vy = -vy)
                const float T = 20.0f, g = 0.12f;
                v[0] = gm::fdiv(gm::fsub(reticle[0], S[0]), T);
                v[1] = gm::fdiv(gm::fsub(gm::fmul(gm::fsub(reticle[1], S[1]), 2.0f), gm::fmul(T, gm::fmul(g, T))), gm::fmul(2.0f, T));
                v[2] = gm::fdiv(gm::fsub(reticle[2], S[2]), T);
                v[1] = -v[1];
                for (float& x : v) x = gm::fmul(x, 0.800000011920929f);
            } else {   // at the lock-on target: straight, 4.5 per tick
                const float d[3] = {gm::fsub(lock_target[0], S[0]), gm::fadd(gm::fsub(lock_target[1], S[1]), 5.0f), gm::fsub(lock_target[2], S[2])};
                const float len = length3(d);
                for (int k = 0; k < 3; ++k) v[k] = len > 0.0f ? gm::fmul(gm::fdiv(d[k], len), 4.5f) : 0.0f;
            }
            if (throw_item) throw_item(id, S, v);
            throw_state = 2;
        }
        return;
    }
    if (throw_state == 2) {
        c.wish = 0x1b;
        if (end - 2.0f <= t && t <= end) {
            c.mode = 0;
            c.can_act = 1;
            c.no_interpolate = true;
            c.held = 0;
            c.wish = 0;
            throw_state = -1;
        }
    }
}

}  // namespace dccore
