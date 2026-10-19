// Ungaga's and Goro's attacks: UngagaKey_On / UngagaKey_Play (overlay 0x243260) and GoroKey_On / GoroKey_Play (overlay 0x244050 / 0x244130). Ported from the decompiled
// dungeon overlay in the same style as Toan's sword (player_attack.cpp). The shot effects they start (NowMainEffect) live in monster_combat.cpp (player_fx) and shots.cpp.
#include <algorithm>
#include <cmath>
#include "dccore/gamemath.h"
#include "dccore/vu0.h"
#include "dccore/libm_ps2.h"
#include "dccore/motion.h"
#include "dccore/walk.h"

namespace dccore {

namespace {
float rot_of(const Walker& w) { return w.player.yaw; }
}

// UngagaKey_On
void Walker::ungaga_key_on() {
    PlayerCombat& c = combat;
    if (0 < c.cooldown) return;
    status->set<int32_t>(0x4334, 1);
    if (c.mode == 0) {
        c.combo_window = 0;
        c.action = 0x25;
        c.mode = 1;
        c.released = 0;
        c.charge = 1.0f;
        c.charge_stage = 0;
        c.no_interpolate = true;
        motion.time = 136.0f;
        c.in_swing = 0;
        c.chain = 0;
        c.idle_counter = 0;
        c.turn_to = rot_of(*this);
    } else {
        if (c.action == 0x25 && c.in_swing != 0 && 40.0f <= c.gauge) {
            c.chain = 1;
            c.combo_window = 0;
        }
        if (c.action == 0x26 && c.in_swing != 0 && 40.0f <= c.gauge) {
            c.chain = 1;
            c.combo_window = 0;
        }
    }
}

// UngagaKey_Play
void Walker::ungaga_key_play(bool ok_on, float dir_x, float dir_z) {
    PlayerCombat& c = combat;
    const float t = motion.time;
    const WeaponView w = status->weapon(status->character());
    int dmg = w.attack();
    if (status_err(8)) dmg <<= 1;
    auto swing = [&](float kick_speed, float decay) {
        for (const float* tip : {weapon_tip2, weapon_tip}) {   // dcol0 then dcol1
            pool->set(6.0f, 0.0f, tip, dmg, 1, 2, 2, 0, 0);
            basic_damage(0, 4);
            pool->set_kickback(char_pos_, kick_speed, decay, 2);
        }
    };
    auto in = [&](float lo, float hi) { return lo <= t && t <= hi; };
    if (in(674.0f, 677.0f)) swing(1.2000000476837158f, 0.10000000149011612f);
    if (in(686.0f, 692.0f)) swing(1.5f, 0.10000000149011612f);
    if (in(700.0f, 704.0f)) swing(2.0f, 0.10000000149011612f);
    for (int idx : {37, 38, 39})   // inside the damaging part of a combo swing: the gauge does not refill
        if (float(motion_table[size_t(idx)].start) < t && t < float(motion_table[size_t(idx)].end)) c.hit_window = 1;
    // (the weapon trail and the sounds are not ported)
    c.lunge_dir[0] = ps2_sinf(player.yaw);
    c.lunge_dir[1] = 0.0f;
    c.lunge_dir[2] = ps2_cosf(player.yaw);
    auto end_of = [&](int idx) { return float(motion_table[size_t(idx)].end); };
    auto stop = [&]() {
        c.wish = 0;
        c.mode = 0;
        c.lunge = 0.0f;
        c.lunge_dec = 0.0f;
    };
    auto cost = [&]() {
        c.gauge = gm::fsub(c.gauge, 20.0f);
        if (c.gauge <= 0.0f) c.gauge = 0.0f;
    };

    if (c.action == 0x25) {
        if (!ok_on) c.released = 1;
        c.wish = 0x25;
        c.in_swing = 1;
        if (end_of(0x25) - 1.0f <= t && t <= end_of(0x25)) {
            if (c.chain == 1) {
                cost();
                c.action = 0x26;
                c.wish = 0x26;
                c.no_interpolate = true;
                c.idle_counter = 0;
                c.in_swing = 0;
                c.chain = 0;
                c.turn_to = ps2_atan2f(dir_x, dir_z);
            } else {
                if (c.released == 0) {   // the button is still down: charge
                    c.no_interpolate = true;
                    c.wish = 0xd;
                    c.action = 0xd;
                    c.in_swing = 1;
                    return;
                }
                stop();
                c.cooldown = 0x14;
            }
        }
    }
    if (c.action == 0x26) {
        if (679.0f <= t && t <= 679.5f) {
            c.lunge = 0.25f;
            c.lunge_dec = 0.0f;
        }
        if (!ok_on) c.released = 1;
        c.wish = 0x26;
        c.in_swing = 1;
        if (end_of(0x26) - 0.5f <= t && t <= end_of(0x26)) {
            if (c.chain == 1) {
                cost();
                c.action = 0x27;
                c.wish = 0x27;
                c.no_interpolate = true;
                c.idle_counter = 0;
                c.in_swing = 0;
                c.chain = 0;
            } else {
                stop();
            }
        }
    }
    if (c.action == 0x27) {
        if (699.0f <= t && t <= 699.5f) {
            c.lunge = 0.0f;
            c.lunge_dec = 0.0f;
        }
        if (!ok_on) c.released = 1;
        c.wish = 0x27;
        c.in_swing = 1;
        if (end_of(0x27) - 1.0f <= t && t <= end_of(0x27)) stop();
    }
    if (c.action == 0xd) {   // the button is still held after the first swing: wind up
        if (!ok_on) c.released = 1;
        c.wish = 0xd;
        if (c.released != 0) {
            c.wish = 0;
            c.mode = 0;
            if (c.lock_on != 0) c.wish = 0x12;
        }
        if (end_of(0xd) - 2.0f <= t && t <= end_of(0xd)) {
            c.wish = 0xe;
            c.action = 0xe;
        }
    }
    if (c.action == 0xe) {   // held: after 30 ticks the charged shot leaves
        if (!ok_on) c.released = 1;
        c.wish = 0xe;
        ++c.shot_wait;
        if (0x1d < c.shot_wait) {
            c.shot_wait = 0;
            if (main_shot) {
                MainShot m{};
                for (int k = 0; k < 3; ++k) m.pos[k] = weapon_origin[k];
                m.target[0] = gm::fadd(ps2_sinf(player.yaw), m.pos[0]);   // getCharacterVector: the way he faces
                m.target[1] = gm::fadd(0.0f, m.pos[1]);
                m.target[2] = gm::fadd(ps2_cosf(player.yaw), m.pos[2]);
                m.owner_kind = 4;
                m.sub_id = 2;
                m.damage = int(double(dmg) * 1.5);
                m.wait = 0x2d;
                main_shot(m);
            }
            if (sword_dmg) sword_dmg(0.800000011920929f);
        }
        if (c.released != 0) {
            c.wish = 0xf;
            c.action = 0xf;
        }
    }
    if (c.action == 0xf) {
        c.wish = 0xf;
        if (end_of(0xf) - 2.0f <= t && t <= end_of(0xf)) {
            c.wish = 0;
            c.mode = 0;
            if (c.lock_on != 0) c.wish = 0x12;
            if (end_main_shot) end_main_shot();
        }
    }
}

// BattleActionOn_Ozumond / _H / _F: which one the weapon is decides.
void Walker::osmond_key_on() {
    PlayerCombat& c = combat;
    if (c.mode != 0) return;
    if (osmond_type == 1) {
        if (gm::ftoi(c.gauge) != 100) return;
        c.action = 0xb;
        c.mode = 1;
        c.released = 0;
        c.charge_stage = 0;
        c.gauge = 0.0f;
        c.charge = 30.0f;
        c.no_interpolate = true;
    } else {
        if (c.overheat != 0) return;
        c.action = osmond_type == 2 ? 0 : 0xb;
        c.mode = 1;
        c.released = 0;
        c.charge_stage = 0;
        if (osmond_type == 0) c.charge = 1.0f;
        else c.osmond_dmg_wait = 0;
        c.no_interpolate = true;
    }
}

// BattleActionPlay_Ozumond (machine gun) / _H (hand cannon) / _F (flame thrower), normal view.
void Walker::osmond_key_play(bool ok_on) {
    PlayerCombat& c = combat;
    const WeaponView w = status->weapon(5);
    int atk = w.attack();
    if (status_err(8)) atk <<= 1;
    float fwd[3] = {ps2_sinf(player.yaw), 0.0f, ps2_cosf(player.yaw)};   // getCharacterVector
    auto aim = [&](float pos[3], float dir[3]) {
        for (int k = 0; k < 3; ++k) pos[k] = weapon_tip2[k];   // the weapon's dcol0
        for (int k = 0; k < 3; ++k) dir[k] = fwd[k];
    };
    auto stop = [&]() {
        c.wish = 0;
        c.mode = 0;
        if (c.lock_on != 0) c.wish = 0x12;
    };
    if (osmond_type == 0) {   // OpC_InitProcess5: a burst of bullets every 10 ticks while the button is held; the gauge is the ammunition
        const int dmg0 = (int(w.attack()) >> 1) + (int(w.magic()) >> 1);
        const int dmg = status_err(8) ? dmg0 * 2 : dmg0;
        const float step = gm::fmul(0.009999999776482582f, gm::itof(w.recharge()));
        if (c.action == 0xb) {
            if (!ok_on) c.released = 1;
            c.gauge = gm::fsub(c.gauge, gm::fsub(1.5f, step));
            if (c.gauge <= 0.0f) {
                c.mode = 0;
                c.overheat = 1;
            }
            c.wish = 0xb;
            float pos[3], dir[3];
            aim(pos, dir);
            float target[3] = {gm::fadd(dir[0], pos[0]), gm::fadd(dir[1], pos[1]), gm::fadd(dir[2], pos[2])};
            if (c.lock_on != 0)
                for (int k = 0; k < 3; ++k) dir[k] = gm::fsub(lock_target[k], pos[k]);
            if (++c.osmond_s_cnt > 9) {
                c.osmond_s_cnt = 0;
                if (main_shot) {   // the muzzle flash hangs on the weapon
                    MainShot m{};
                    for (int k = 0; k < 3; ++k) { m.pos[k] = pos[k]; m.target[k] = target[k]; }
                    m.owner_kind = -1;
                    m.sub_id = -1;
                    m.attach = true;
                    m.set_attribute = false;
                    m.set_damage = false;
                    main_shot(m);
                }
            }
            if (++c.osmond_msg_cnt > 9) {
                c.osmond_msg_cnt = 0;
                float v[3] = {dir[0], dir[1], dir[2]};
                const vu0::V3 n = vu0::normalize(v);
                for (int k = 0; k < 3; ++k) v[k] = gm::fmul(10.0f, n[k]);
                if (fire_mg) fire_mg(pos, v, dmg);
                c.idle_counter = 0;
                if (sword_dmg) sword_dmg(0.5f);
            }
            if (c.released == 1) {
                stop();
                if (off_main_effect) off_main_effect(-1);
            }
        }
    } else if (osmond_type == 1) {   // Ozumond_H: one strong shot
        if (c.action == 0xb) {
            if (!ok_on) {
                c.released = 1;
                c.osmond_wait = 10;
            }
            c.wish = 0xb;
            ++c.osmond_wait;
            if (9 < c.osmond_wait) {
                c.osmond_wait = 0;
                float pos[3], dir[3];
                aim(pos, dir);
                MainShot m{};
                for (int k = 0; k < 3; ++k) m.pos[k] = pos[k];
                if (c.lock_on == 0) {
                    m.target[0] = gm::fadd(dir[0], pos[0]); m.target[1] = gm::fadd(dir[1], pos[1]); m.target[2] = gm::fadd(dir[2], pos[2]);
                } else {
                    for (int k = 0; k < 3; ++k) m.target[k] = lock_target[k];
                }
                m.owner_kind = 5;
                m.sub_id = w.element_choice();
                m.damage = atk;
                m.wait = 0;
                if (main_shot) main_shot(m);
                c.released = 1;
                c.idle_counter = 0;
                if (sword_dmg) sword_dmg(2.0f);
            }
            if (c.released == 1) stop();
        }
    } else {   // Ozumond_F: the flame thrower
        if (c.action == 0) {
            c.wish = 0xb;
            if (7 < ++c.osmond_dmg_wait) {
                c.osmond_dmg_wait = 0;
                if (sword_dmg) sword_dmg(1.5f);
                c.action = 1;
                c.idle_counter = 0;
                float pos[3], dir[3];
                aim(pos, dir);
                if (firebar_init) firebar_init(pos, dir);
            }
            if (!ok_on) {
                stop();
                c.osmond_wait = 10;
            }
        }
        if (c.action == 1) {
            c.wish = 0xb;
            c.gauge = gm::fsub(c.gauge, 0.5f);
            if (c.gauge <= 0.0f) {
                stop();
                c.overheat = 1;
            }
            float pos[3], dir[3];
            aim(pos, dir);
            if (firebar_set) firebar_set(pos, dir, atk, w.element_choice());
            if (29 < ++c.osmond_dmg_wait) {
                c.osmond_dmg_wait = 0;
                if (sword_dmg) sword_dmg(2.200000047683716f);
                c.idle_counter = 0;
            }
            if (!ok_on) {
                stop();
                if (firebar_reset) firebar_reset();
                c.osmond_wait = 10;
                c.osmond_dmg_wait = 0;
            }
        }
    }
}

// BattleActionOn_Ruby: the magic staff needs a full gauge.
void Walker::ruby_key_on() {
    PlayerCombat& c = combat;
    if (c.mode != 0 || gm::ftoi(c.gauge) != 100) return;
    c.released = 0;
    c.action = 0xb;
    c.mode = 1;
    c.gauge = 0.0f;
    c.charge = 0.0f;
    c.no_interpolate = true;
}

// BattleActionShotRuby (normal view): a magic ball from the "weapon" frame toward where she faces (or the locked-on monster).
void Walker::ruby_shot(float scale, bool second) {
    const WeaponView w = status->weapon(status->character());
    int dmg = gm::ftoi(gm::fmul(gm::itof((int(w.attack()) >> 1) + (int(w.magic()) >> 1)), scale));
    if (status_err(8)) dmg <<= 1;
    if (!main_shot) return;
    MainShot m{};
    if (!second || !ruby_shot_valid) {
        for (int k = 0; k < 3; ++k) m.pos[k] = weapon_origin[k];
        if (combat.lock_on == 0) {
            m.target[0] = gm::fadd(ps2_sinf(player.yaw), m.pos[0]);   // getCharacterVector + the position
            m.target[1] = gm::fadd(0.0f, m.pos[1]);
            m.target[2] = gm::fadd(ps2_cosf(player.yaw), m.pos[2]);
        } else {
            for (int k = 0; k < 3; ++k) m.target[k] = lock_target[k];
        }
        for (int k = 0; k < 3; ++k) { ruby_pos[k] = m.pos[k]; ruby_vec[k] = m.target[k]; }
        ruby_shot_valid = true;
    } else {
        for (int k = 0; k < 3; ++k) { m.pos[k] = ruby_pos[k]; m.target[k] = ruby_vec[k]; }
        m.random_rate = 2.0f;
        m.no_sound = true;
    }
    m.owner_kind = 3;
    m.sub_id = w.element_choice();
    m.damage = dmg;
    m.force_phase = 1;
    m.set_attribute = false;
    main_shot(m);
}

// BattleActionPlay_Ruby (normal view): draw (0xb), a quick tap fires a ball; held: the charge (0xe), released: one or two stronger balls (0x10).
void Walker::ruby_key_play(bool ok_on) {
    PlayerCombat& c = combat;
    const float t = motion.time;
    auto end_of = [&](int idx) { return float(motion_table[size_t(idx)].end); };
    auto reset = [&]() {
        c.wish = 0;
        c.mode = 0;
        if (c.lock_on != 0) c.wish = 0x12;
    };
    if (c.action == 0xb) {
        if (!ok_on) c.released = 1;
        c.wish = 0xb;
        c.effect_id = -1;
        if (126.0f <= t && t < 126.30000305175781f && c.released == 0) {
            c.wish = 0xe;
            c.action = 0xe;
            c.charge = 0.0f;
            if (main_shot) {   // the charge effect hangs on her hand
                MainShot m{};
                m.owner_kind = 3;
                m.sub_id = status->weapon(status->character()).element_choice();
                m.force_phase = 0;
                m.attach = true;
                m.set_attribute = false;
                m.set_damage = false;
                m.loop = 1;
                c.effect_id = main_shot(m);
            }
        }
        if (128.0f <= t && t < 128.3000030517578f) {
            ruby_shot(1.0f, false);
            c.idle_counter = 0;
            if (sword_dmg) sword_dmg(0.800000011920929f);
        }
        if (end_of(0xb) - 1.0f <= t && t <= end_of(0xb)) reset();
    }
    if (c.action == 0xe) {
        if (!ok_on) c.released = 1;
        c.wish = 0xe;
        c.charge = gm::fadd(c.charge, 1.0f);
        if (60.0f <= c.charge) c.charge = 60.0f;
        if (c.released != 0) {
            c.action = 0x10;
            c.gauge = 0.0f;
        }
    }
    if (c.action == 0x10) {
        c.wish = 0x10;
        if (60.0f <= c.charge) {
            if (216.0f <= t && t < 217.5f && c.effect_id != -1) {
                if (off_main_effect) off_main_effect(c.effect_id);
                c.effect_id = -1;
            }
            if (216.0f <= t && t < 216.3999938964844f) {
                ruby_shot(2.200000047683716f, false);
                ruby_shot(2.200000047683716f, true);
                c.idle_counter = 0;
                if (sword_dmg) sword_dmg(1.7999999523162842f);
            }
        } else {
            if (216.5f <= t && t < 217.5f && c.effect_id != -1) {
                if (off_main_effect) off_main_effect(c.effect_id);
                c.effect_id = -1;
            }
            if (216.5f <= t && t < 216.8999938964844f) {
                ruby_shot(1.5f, false);
                c.idle_counter = 0;
                if (sword_dmg) sword_dmg(1.2000000476837158f);
            }
        }
        if (end_of(0x10) - 1.0f <= t && t <= end_of(0x10)) reset();
    }
}

// BattleActionOn_Jinn (Xiao): the slingshot needs a full gauge.
void Walker::xiao_key_on() {
    PlayerCombat& c = combat;
    if (c.mode != 0 || gm::ftoi(c.gauge) != 100) return;
    c.released = 0;
    c.charge = 0.0f;
    c.action = 0xb;
    c.mode = 1;
    c.gauge = 0.0f;
    c.no_interpolate = true;
}

// BattleActionPlay_Jinn (Xiao, normal view): draw back (0xb), hold (0xc), release (0xd): the bullet leaves at motion time 251..252.
void Walker::xiao_key_play(bool ok_on) {
    PlayerCombat& c = combat;
    const float t = motion.time;
    int dmg = status->weapon(status->character()).attack();
    if (status_err(8)) dmg <<= 1;
    auto end_of = [&](int idx) { return float(motion_table[size_t(idx)].end); };
    if (c.action == 0xb) {
        if (!ok_on) c.released = 1;
        c.wish = 0xb;
        if (end_of(0xb) - 2.0f <= t && t <= end_of(0xb)) c.action = c.released == 0 ? 0xc : 0xd;
    }
    if (c.action == 0xd) {
        c.wish = 0xd;
        if (251.0f < t && t < 252.0f) {
            Bullet b{};
            for (int k = 0; k < 3; ++k) b.pos[k] = weapon_muzzle[k];
            const float s = ps2_sinf(player.yaw), co = ps2_cosf(player.yaw);   // setShotVector(5, yaw, 0)
            b.vel[0] = gm::fmul(5.0f, s);
            b.vel[1] = 0.0f;
            b.vel[2] = gm::fmul(5.0f, co);
            b.damage = dmg;
            if (fire_bullet) fire_bullet(b);
            c.idle_counter = 0;
            if (sword_dmg) sword_dmg(1.0f);
        }
        if (end_of(0xd) - 2.0f <= t && t <= end_of(0xd)) {
            c.wish = 0;
            c.mode = 0;
            if (c.lock_on != 0) c.wish = 0x12;
        }
    }
    if (c.action == 0xc) {
        if (!ok_on) c.released = 1;
        c.wish = 0xc;
        c.no_interpolate = true;
        if (c.released != 0) {
            c.no_interpolate = true;
            c.action = 0xd;
        }
    }
}

// GoroKey_On
void Walker::goro_key_on() {
    PlayerCombat& c = combat;
    if (c.mode != 0 || c.gauge < 100.0f) return;
    c.action = 0x24;
    c.mode = 1;
    c.released = 0;
    c.charge = 1.0f;
    c.gauge = 0.0f;
    c.charge_stage = 0;
    c.no_interpolate = true;
    motion.time = 136.0f;
    c.in_swing = 0;
    c.chain = 0;
    c.idle_counter = 0;
    c.turn_to = rot_of(*this);
}

// GoroKey_Play
void Walker::goro_key_play(bool ok_on, float dir_x, float dir_z) {
    (void)dir_x;
    (void)dir_z;
    PlayerCombat& c = combat;
    const float t = motion.time;
    const WeaponView w = status->weapon(status->character());
    int64_t dmg = w.attack();
    if (status_err(8)) dmg <<= 1;
    auto in = [&](float lo, float hi) { return lo <= t && t <= hi; };
    if (in(99.0f, 102.0f)) {
        pool->set(5.0f, 0.0f, weapon_tip2, int(dmg), 1, 2, 2, 0, 0);
        basic_damage(0, 2);
        pool->set_kickback(char_pos_, 2.5f, 0.10000000149011612f, 2);
    }
    if (in(462.0f, 482.0f)) {
        dmg = int64_t(double(dmg) * 1.5);
        pool->set(5.0f, 0.0f, weapon_tip2, int(dmg), 1, 2, 2, 0, 0);
        basic_damage(1, 2);
    }
    if (482.0f <= t && t < 482.3999938964844f) {
        if (main_shot) {
            MainShot m{};
            for (int k = 0; k < 3; ++k) m.pos[k] = weapon_tip2[k];
            m.target[0] = 0.0010000000474974513f; m.target[1] = 1.0f; m.target[2] = 0.0010000000474974513f;
            m.owner_kind = 2;
            m.sub_id = -1;
            m.damage = gm::ftoi(gm::fmul(1.2000000476837158f, gm::itof(int(dmg))));
            m.wait = 0x2d;
            main_shot(m);
        }
        pool->set_kickback(char_pos_, 2.5f, 0.10000000149011612f, 2);
        c.recoil[0] = 8.0f;
        c.recoil[1] = 0.10000000149011612f;
        c.recoil_time = 0x1e;
    }
    if (103.0f <= t && t < 103.4000015258789f) {
        c.recoil[0] = 2.0f;
        c.recoil[1] = 0.009999999776482582f;
        c.recoil_time = 0x14;
    }
    c.lunge_dir[0] = ps2_sinf(player.yaw);
    c.lunge_dir[1] = 0.0f;
    c.lunge_dir[2] = ps2_cosf(player.yaw);
    auto end_of = [&](int idx) { return float(motion_table[size_t(idx)].end); };
    if (c.action == 0x24) {
        if (in(94.5f, 95.0f)) {
            c.lunge = 0.25f;
            c.lunge_dec = 0.009999999776482582f;
        }
        c.wish = 0x24;
        if (!ok_on) c.released = 1;
        c.in_swing = 1;
        if (in(95.0f, 95.5f) && c.released == 0) {   // the button is still down: wind up the big attack
            c.wish = 0xd;
            c.action = 0xd;
            c.released = 0;
            c.in_swing = 1;
            c.lunge = 0.0f;
            c.lunge_dec = 0.0f;
            return;
        }
        if (end_of(0x24) - 1.0f <= t && t <= end_of(0x24)) {
            c.wish = 0;
            c.mode = 0;
            c.lunge = 0.0f;
            c.lunge_dec = 0.0f;
            c.in_swing = 0;
            c.chain = 0;
        }
    }
    if (c.action == 0xd) {
        c.released += 1;   // (counts the ticks the button is held)
        if (0x2c < c.released) c.released = 0x2e;
        c.wish = 0xd;
        if (!ok_on) {
            if (c.released < 0x2d) {   // let go early: just the plain attack, for 15 of the gauge
                c.mode = 1;
                c.released = 0;
                c.charge = 1.0f;
                c.gauge = gm::fsub(c.gauge, 15.0f);
                c.charge_stage = 0;
                c.in_swing = 0;
                c.chain = 0;
                c.idle_counter = 0;
                c.turn_to = rot_of(*this);
                c.action = 0x24;
            } else {   // fully wound up: the big attack
                c.action = 0xe;
                c.gauge = 0.0f;
                c.held = 1;
                c.can_act = 0;
                c.busy = 999;
                if (sword_dmg) sword_dmg(2.0f);
            }
        }
    }
    if (c.action == 0xe) {
        c.wish = 0xe;
        if (end_of(0xe) - 1.0f <= t && t <= end_of(0xe)) {
            c.wish = 0;
            c.mode = 0;
            c.held = 0;
            c.busy = 0;
            c.can_act = 1;
            if (c.lock_on != 0) c.wish = 0x12;
        }
    }
}

}  // namespace dccore
