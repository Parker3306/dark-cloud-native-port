// Toan's sword: ToanKey_On / ToanKey_Play (overlay 0x241450 / 0x241690), the weapon's hit sphere and the counters motionDrive keeps for the player (attack gauge,
// cooldowns). Ported from the decompiled dungeon overlay and checked against the running game with tools/dc/record_attack.py / validate_attack.py.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "dccore/gamemath.h"
#include "dccore/libm_ps2.h"
#include "dccore/motion.h"
#include "dccore/walk.h"

namespace dccore {

void Walker::setup_weapon(const Character* player_model, const float tip_local[3], const float tip2_local[3], const Character* weapon_model_in) {
    model = player_model;
    weapon_model = weapon_model_in;
    muzzle_joint = -1;
    if (weapon_model)
        for (size_t i = 0; i < weapon_model->joints.size(); ++i)
            if (weapon_model->joints[i].name == "eff30") muzzle_joint = int(i);
    for (int k = 0; k < 3; ++k) {
        weapon_tip_local[k] = tip_local[k];
        weapon_tip2_local[k] = tip2_local ? tip2_local[k] : tip_local[k];
    }
    weapon_frame = -1;
    if (model)
        for (size_t i = 0; i < model->joints.size(); ++i)
            if (model->joints[i].name == "weapon") weapon_frame = int(i);
    item_frame = -1;
    if (model)
        for (size_t i = 0; i < model->joints.size(); ++i)
            if (model->joints[i].name == "item") item_frame = int(i);
}

// The blade's hit spheres (dcol0 / dcol1 of the weapon model) the way the attack code sees them: the frame positions as of the last drawn frame, i.e. the pose of the
// time before the last motion step, with the character placed where MoveChara left it. The weapon model hangs on the player model's animated "weapon" object.
void Walker::update_weapon_tip() {
    if (!model || model->skeleton.size() == 0) return;
    const PoseSample& sm = last_sample;
    const Pose pose = sm.blend ? evaluate_pose_blend(model->skeleton, model->tracks, double(sm.frame), double(sm.next), double(sm.frac))
                               : evaluate_pose(model->skeleton, model->tracks, double(sm.time));
    if (item_frame >= 0) {   // the hand: where a thrown item starts
        const Mat4& iw = pose.world[size_t(item_frame)];
        const float s0 = ps2_sinf(player.yaw), c0 = ps2_cosf(player.yaw);
        const float bx = float(iw[12]), by = float(iw[13]), bz = float(iw[14]);
        item_origin[0] = gm::fadd(player.pos[0], gm::fadd(gm::fmul(bx, c0), gm::fmul(bz, s0)));
        item_origin[1] = gm::fadd(player.pos[1], by);
        item_origin[2] = gm::fadd(player.pos[2], gm::fsub(gm::fmul(bz, c0), gm::fmul(bx, s0)));
        item_origin[3] = 1.0f;
    }
    if (weapon_frame < 0) return;
    const Mat4& w = pose.world[size_t(weapon_frame)];
    const float s = ps2_sinf(player.yaw), c = ps2_cosf(player.yaw);
    auto place = [&](const float local[3], float out[4]) {
        const double q0 = local[0], q1 = local[1], q2 = local[2];
        const float bx = float(q0 * w[0] + q1 * w[4] + q2 * w[8] + w[12]);
        const float by = float(q0 * w[1] + q1 * w[5] + q2 * w[9] + w[13]);
        const float bz = float(q0 * w[2] + q1 * w[6] + q2 * w[10] + w[14]);
        out[0] = gm::fadd(player.pos[0], gm::fadd(gm::fmul(bx, c), gm::fmul(bz, s)));
        out[1] = gm::fadd(player.pos[1], by);
        out[2] = gm::fadd(player.pos[2], gm::fsub(gm::fmul(bz, c), gm::fmul(bx, s)));
        out[3] = 1.0f;
    };
    place(weapon_tip_local, weapon_tip);
    place(weapon_tip2_local, weapon_tip2);
    const float zero[3] = {0, 0, 0};
    place(zero, weapon_origin);
    if (weapon_model && muzzle_joint >= 0 && weapon_model->skeleton.size() != 0) {   // the weapon's own pose at the same time hangs on the "weapon" frame
        const Pose wp = sm.blend ? evaluate_pose_blend(weapon_model->skeleton, weapon_model->tracks, double(sm.frame), double(sm.next), double(sm.frac))
                                 : evaluate_pose(weapon_model->skeleton, weapon_model->tracks, double(sm.time));
        const Mat4 wm = mat_mul(wp.world[size_t(muzzle_joint)], w);
        const float bx = float(wm[12]), by = float(wm[13]), bz = float(wm[14]);
        weapon_muzzle[0] = gm::fadd(player.pos[0], gm::fadd(gm::fmul(bx, c), gm::fmul(bz, s)));
        weapon_muzzle[1] = gm::fadd(player.pos[1], by);
        weapon_muzzle[2] = gm::fadd(player.pos[2], gm::fsub(gm::fmul(bz, c), gm::fmul(bx, s)));
        weapon_muzzle[3] = 1.0f;
    }
}

// The screen shake of heavy attacks (effects step): while it lasts the camera follow height jumps by a random amount within +-recoil[0].
void Walker::step_shake(GameRand& rng) {
    PlayerCombat& c = combat;
    const float amount = c.recoil[0];
    if (c.recoil_time < 1) {
        params.follow_height2 = 0.0f;
    } else {
        const int32_t r = rng.next();
        params.follow_height2 = gm::fsub(gm::fdiv(gm::fmul(gm::fmul(amount, gm::itof(r)), 2.0f), gm::kRandRange), amount);
        c.recoil[0] = gm::fsub(c.recoil[0], c.recoil[1]);
        --c.recoil_time;
    }
}

// The counters motionDrive keeps for the player after the CCharacter step.
void Walker::step_drive_counters() {
    if (!status || drive_step_hold) return;
    PlayerCombat& c = combat;
    const int ch = status->character();
    if (0 < c.event_lock) --c.event_lock;
    if (status_err(4) || status_err(8)) {   // timed status effects wear off
        const size_t at = 0x42e0 + size_t(ch) * 2;
        if (0 < status->get<int16_t>(at)) {
            status->set<int16_t>(at, int16_t(status->get<int16_t>(at) - 1));
            if (status->get<int16_t>(at) == 0) {
                status->set<uint32_t>(0x42c8 + size_t(ch) * 4, 0);
                c.frozen = 0;
            }
        }
    }
    if (0 < c.cooldown) --c.cooldown;
    const WeaponView w = status->weapon(ch);
    if (gm::ftoi(c.gauge) < 100) {
        if (c.combo_window < 10) ++c.combo_window;
        float f12 = gm::fdiv(gm::itof(w.recharge()), 30.0f);
        if (status_err(0x40)) f12 = gm::fdiv(f12, 2.0f);
        if (status_err(8)) f12 = gm::fmul(f12, 2.0f);
        float f13 = 1.0f;
        if (1.0f <= f12) f13 = f12;
        if (ch == 0 || ch == 4) {
            if (c.hit_window == 0) f13 = gm::fmul(f13, 0.25f);
            else f13 = 0.0f;
            c.hit_window = 0;
        }
        if (ch == 5) {   // Osmond: the hand cannon refills fast, the others only once the gun has run dry
            if (osmond_type == 1) f13 = gm::fmul(f13, 1.5f);
            else if (c.overheat == 0) f13 = 0.0f;
            else f13 = gm::fmul(f13, 0.5f);
        }
        if (ch == 1 || ch == 3) f13 = gm::fmul(f13, 1.5f);
        c.gauge = gm::fadd(c.gauge, f13);
        if (99 < gm::ftoi(c.gauge)) {
            c.gauge = 100.0f;
            c.gauge_full = 10;
            c.overheat = 0;
        }
    } else if (0 < c.gauge_full) {
        --c.gauge_full;
    }
    ++c.idle_counter;
    if (0xe0f < c.idle_counter) c.idle_counter = 0xe10;
    ++c.heal_counter;
    if (0xef < c.heal_counter) {
        if ((uint16_t(w.status_flags()) & 0x800) != 0 && 0 < status->life(ch)) add_now_life(255.0f, ch, 1);
        c.heal_counter = 0;
    }
}

// basic_damage(sub, owner): what the weapon adds to the hit box it just made.
void Walker::basic_damage(int sub_id, int owner_kind) {
    ColEntry& e = pool->e[size_t(pool->last)];
    const WeaponView w = status->weapon(status->character());
    static constexpr int kElement[6] = {1, 2, 4, 8, 0x10, 0};   // element_tbl
    int choice = w.element_choice();
    if (choice < 0 || 5 < choice) choice = 5;
    e.slayer = w.slayer();
    e.status_flags = w.status_flags();
    e.flags = kElement[choice];
    e.owner_kind = owner_kind;
    e.sub_id = sub_id;
}

// ToanKey_On: the attack button went down.
void Walker::toan_key_on() {
    PlayerCombat& c = combat;
    if (0 < c.cooldown) return;
    if (c.mode == 0) {
        c.combo_window = 0;
        c.action = 0x24;
        c.mode = 1;
        c.released = 0;
        c.charge = 1.0f;
        c.charge_stage = 0;
        c.no_interpolate = true;
        motion.time = 136.0f;
        c.in_swing = 0;
        c.chain = 0;
        c.idle_counter = 0;
        c.turn_to = player.yaw;
    } else if ((c.action == 0x24 || c.action == 0x25 || c.action == 0x26 || c.action == 0x27) && c.in_swing != 0 && 20.0f <= c.gauge) {
        c.chain = 1;
        c.combo_window = 0;
    }
}

// ToanKey_Play: every tick while the player attacks.
void Walker::toan_key_play(bool ok_on, float dir_x, float dir_z) {
    PlayerCombat& c = combat;
    const float t = motion.time;
    const WeaponView w = status->weapon(0);
    int dmg = w.attack();
    if (status_err(8)) dmg <<= 1;
    auto swing = [&](float radius, int damage, int sub, float speed, float decay) {
        pool->set(radius, 0.0f, weapon_tip, damage, 1, 2, 2, 0, 0);
        basic_damage(sub, 0);
        pool->set_kickback(char_pos_, speed, decay, 2);
    };
    auto in = [&](float lo, float hi) { return lo <= t && t <= hi; };
    if (in(825.0f, 828.0f)) swing(2.799999952316284f, dmg, 0, 1.2000000476837158f, 0.20000000298023224f);
    if (in(833.0f, 835.0f)) swing(5.300000190734863f, dmg, 0, 1.2000000476837158f, 0.20000000298023224f);
    if (in(842.0f, 844.0f)) swing(6.199999809265137f, dmg, 0, 1.5f, 0.30000001192092896f);
    if (in(851.0f, 854.0f)) swing(6.199999809265137f, dmg, 0, 2.0f, 0.4000000059604645f);
    if (in(862.0f, 866.0f)) swing(6.199999809265137f, gm::ftoi(gm::fmul(gm::itof(dmg), 1.7999999523162842f)), 0, 3.0f, 0.30000001192092896f);
    if (in(193.0f, 196.0f)) swing(6.0f, int(double(dmg) * 1.5), 2, 3.0f, 0.30000001192092896f);
    if (in(720.0f, 722.0f)) swing(12.0f, int(double(dmg) * 1.5), 3, 3.0f, 0.30000001192092896f);
    for (int idx : {37, 38, 39, 40})   // inside the damaging part of a combo swing: the gauge does not refill
        if (float(motion_table[size_t(idx)].start) < t && t < float(motion_table[size_t(idx)].end)) c.hit_window = 1;
    {   // direction of the forward step: where the character faces
        c.lunge_dir[0] = ps2_sinf(player.yaw);
        c.lunge_dir[1] = 0.0f;
        c.lunge_dir[2] = ps2_cosf(player.yaw);
    }
    auto end_of = [&](int idx) { return float(motion_table[size_t(idx)].end); };
    auto start_of = [&](int idx) { return float(motion_table[size_t(idx)].start); };
    auto near_end = [&](int idx) { return end_of(idx) - 1.0f <= t && t <= end_of(idx); };
    auto cost = [&]() {
        c.gauge = gm::fsub(c.gauge, 25.0f);
        if (c.gauge <= 0.0f) c.gauge = 0.0f;
    };
    auto turn = [&]() {
        if (0.0f < c.stick_vector) player.yaw = unit_rotation(c.turn_to, player.yaw);
    };
    auto goto_action = [&](int action) {
        c.action = action;
        c.wish = action;
        c.no_interpolate = true;
    };
    auto stop = [&]() {
        c.wish = 0;
        c.mode = 0;
        c.lunge = 0.0f;
        c.lunge_dec = 0.0f;
    };

    if (c.action == 0x24) {
        if (in(820.0f, 820.5f)) { c.lunge = 0.17000000178813934f; c.lunge_dec = 0.0f; }
        c.wish = 0x24;
        if (!ok_on) c.released = 1;
        c.in_swing = 1;
        if (near_end(0x24)) {
            if (c.released == 0) {   // the button is still down: charge
                c.action = 0xe;
                c.wish = 0xe;
                c.gauge = 0.0f;
                c.lunge = 0.0f;
            } else if (c.chain == 1) {
                cost();
                goto_action(0x25);
                c.idle_counter = 0;
                c.turn_to = ps2_atan2f(dir_x, dir_z);
            } else {
                stop();
                c.lunge = 0.0f;
                c.lunge_dec = 0.0f;
                c.cooldown = 0xf;
            }
            c.in_swing = 0;
            c.chain = 0;
        }
    }
    if (c.action == 0x25) {
        turn();
        c.wish = 0x25;
        c.in_swing = 1;
        if (near_end(0x25)) {
            if (c.chain == 1) {
                cost();
                goto_action(0x26);
                c.idle_counter = 0;
                c.turn_to = ps2_atan2f(dir_x, dir_z);
            } else {
                stop();
            }
            c.in_swing = 0;
            c.chain = 0;
        }
    }
    if (c.action == 0x26) {
        turn();
        c.wish = 0x26;
        c.in_swing = 1;
        if (near_end(0x26)) {
            if (c.chain == 1) {
                cost();
                goto_action(0x27);
                c.idle_counter = 0;
                c.turn_to = ps2_atan2f(dir_x, dir_z);
            } else {
                stop();
            }
            c.in_swing = 0;
            c.chain = 0;
        }
    }
    if (c.action == 0x27) {
        turn();
        c.wish = 0x27;
        c.in_swing = 1;
        if (near_end(0x27)) {
            if (c.chain == 1) {
                cost();
                goto_action(0x28);
                c.idle_counter = 0;
            } else {
                stop();
            }
            c.in_swing = 0;
            c.chain = 0;
        }
    }
    if (c.action == 0x28) {
        if (867.5f <= t && t <= 868.0f) { c.lunge = 0.0f; c.lunge_dec = 0.0f; }
        c.wish = 0x28;
        if (near_end(0x28)) {
            stop();
            c.in_swing = 0;
            c.chain = 0;
        }
    }
    if (c.action == 0xe) {   // charging
        if (!ok_on) c.released = 1;
        c.wish = 0xe;
        c.charge = gm::fadd(c.charge, 0.016666667535901070f);
        if (1.5f <= c.charge && c.charge_stage == 0) c.charge_stage = 1;
        if (status->get<int32_t>(0x4324) != 0 && 2.5f <= c.charge && c.charge_stage == 1) c.charge_stage = 2;
        if (c.released == 1) {
            if (1.5f <= c.charge) {
                const bool big = status->get<int32_t>(0x4324) != 0 && 2.5f <= c.charge;
                if (3.0f <= c.charge) c.charge = 3.0f;
                if (big) {
                    if (main_shot) {   // the whirlwind: starts at his feet
                        MainShot m{};
                        for (int k = 0; k < 3; ++k) m.pos[k] = char_pos_[k];
                        m.target[0] = 0.00009999999747378752f; m.target[1] = 0.9998000264167786f; m.target[2] = 0.00009999999747378752f;
                        m.owner_kind = 0;
                        m.sub_id = 3;
                        m.damage = gm::ftoi(gm::fmul(gm::itof(dmg), 1.5f));
                        m.wait = 0x2d;
                        main_shot(m);
                    }
                    if (sword_dmg) sword_dmg(3.0f);
                    goto_action(0x18);
                } else {
                    goto_action(0xf);
                }
            } else {
                c.wish = 0;
                c.mode = 0;
            }
        }
    }
    if (c.action == 0x18) {
        c.busy = 6000;
        c.held = 1;
        c.can_act = 0;
        c.wish = 0x18;
        if (near_end(0x18)) {
            c.wish = 0;
            c.mode = 0;
            c.busy = 0;
            c.held = 0;
            c.can_act = 1;
        }
    }
    if (c.action == 0xf) {   // the charged slash: a hop first
        c.wish = 0xf;
        c.busy = 6000;
        c.held = 1;
        c.can_act = 0;
        if (187.0f <= t && t < 188.0f) {
            float from[3] = {char_pos_[0], char_pos_[1], char_pos_[2]};
            const float fx = ps2_sinf(player.yaw), fz = ps2_cosf(player.yaw);   // getCharacterVector: the way he faces
            float to[3] = {gm::fadd(gm::fmul(fx, 16.0f), from[0]), from[1], gm::fadd(gm::fmul(fz, 16.0f), from[2])};
            c.leap = 1;
            const float g = 0.10000000149011612f, T = 40.0f;   // ParabolicInitialVector(g, T, out, from, to)
            const float vy = float((double(float(double(gm::fsub(to[1], from[1])) * 2.0)) - double(T) * double(g) * double(T)) / (double(T) * 2.0));
            c.leap_v[0] = 0.0f;
            c.leap_v[1] = vy * -1.0f;
            c.leap_v[2] = 0.0f;
        }
        if (near_end(0xf)) {
            c.action = 0x10;
            c.wish = 0x10;
            c.no_interpolate = true;
            motion.time = start_of(0x10);
        }
    }
    if (c.action == 0x10) {
        c.wish = 0x10;
        if (c.leap_v[1] <= -0.4000000059604645f) {
            c.action = 3;
            c.no_interpolate = true;
            motion.time = start_of(3);
        }
    }
    if (c.action == 3) {
        c.wish = 3;
        if (near_end(3)) {
            c.action = 0x19;
            c.no_interpolate = true;
            motion.time = start_of(0x19);
        }
    }
    if (c.action == 0x19) {
        c.wish = 0x19;
        if (c.height_above < 2.0f) {
            c.action = 0x11;
            c.leap = 0;
            c.no_interpolate = true;
            motion.time = start_of(0x11);
        }
    }
    if (c.action == 0x11) {
        c.wish = 0x11;
        if (near_end(0x11)) {
            c.wish = 0;
            c.mode = 0;
            c.busy = 0;
            c.held = 0;
            c.can_act = 1;
        }
    }
}

}  // namespace dccore
