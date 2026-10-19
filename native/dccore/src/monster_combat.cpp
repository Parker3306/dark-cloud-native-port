// Monster side of combat: CMonstorUnit::CheckDmg (the hit boxes of the monster go into the pool, the player's attacks in the pool hurt it), the world position of
// the spheres, and what a dead monster leaves behind. Ported from the decompiled functions (CheckDmg @ 0x1d9f10, Step @ 0x1dd540, SelectAttach @ 0x1d9d60);
// all rand() calls are made in the game's order so that fights can be compared tick by tick with the real game.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "dccore/gamemath.h"
#include "dccore/vu0.h"
#include "dccore/libm_ps2.h"
#include "dccore/monster_ai.h"
#include <optional>
#include "dccore/motion.h"

namespace dccore {

using namespace gm;

namespace {
constexpr float kPiF = 3.14159274101257324f;
constexpr float kTwoPiF = 6.28318548202514648f;
constexpr float kDegToRad = 0.01745329238474369f;

float percent_roll(GameRand& rng) {   // (float)rand() * 100.0 / 2.1474836e9
    return fdiv(fmul(itof(rng.next()), 100.0f), kRandRange);
}

int ceil_to_int(float v) {
    int i = ftoi(v);
    if (0.0f < fsub(v, itof(i))) ++i;
    return i;
}

int element_index(int flags) {
    switch (flags) {
        case 0x10: return 4;
        case 8: return 3;
        case 4: return 2;
        case 2: return 1;
        case 1: return 0;
        default: return 5;
    }
}
}  // namespace

void MonsterWorld::update_cols(MonsterActor& a, const PoseSample& sample) {
    const MonsterBase& base = unit_->bases[size_t(a.base)];
    if (!base.model) return;
    const Character& ch = *base.model;
    auto find_joint = [&](const std::string& name) {
        for (size_t i = 0; i < ch.joints.size(); ++i)
            if (ch.joints[i].name == name) return int(i);
        return -1;
    };
    bool any = false;
    for (BodyCol& b : a.body_cols)
        if (b.active) {
            if (b.joint < 0) b.joint = find_joint(b.bone);
            any = any || b.joint >= 0;
        }
    for (DmgCol& d : a.dmg_cols)
        if (d.active) {
            if (d.joint < 0) d.joint = find_joint(d.bone);
            any = any || d.joint >= 0;
        }
    for (MovCol& d : a.mov_cols)
        if (d.active) {
            if (d.joint < 0) d.joint = find_joint(d.bone);
            any = any || d.joint >= 0;
        }
    for (const ShotRegistration& r : a.shot)
        if (r.state == 1 && r.joint >= 0) any = true;
    if (!a.lock_bone.empty() && a.lock_joint >= 0) any = true;
    if (!any) return;
    const Pose pose = sample.blend ? evaluate_pose_blend(ch.skeleton, ch.tracks, double(sample.frame), double(sample.next), double(sample.frac)) : evaluate_pose(ch.skeleton, ch.tracks, double(sample.time));
    const float s = ps2_sinf(a.rot[1]), c = ps2_cosf(a.rot[1]);
    auto world = [&](int joint, float out[4]) {
        const Mat4& m = pose.world[size_t(joint)];
        const float bx = float(m[12]), by = float(m[13]), bz = float(m[14]);
        out[0] = fadd(a.pos[0], fadd(fmul(bx, c), fmul(bz, s)));
        out[1] = fadd(a.pos[1], by);
        out[2] = fadd(a.pos[2], fsub(fmul(bz, c), fmul(bx, s)));
        out[3] = 1.0f;
    };
    for (BodyCol& b : a.body_cols)
        if (b.active && b.joint >= 0) world(b.joint, b.pos);
    for (DmgCol& d : a.dmg_cols)
        if (d.active && d.joint >= 0) world(d.joint, d.pos);
    if (!a.lock_bone.empty() && a.lock_joint >= 0) world(a.lock_joint, a.lock_pos);
    for (ShotRegistration& r : a.shot)
        if (r.state == 1 && r.joint >= 0) {
            world(r.joint, r.muzzle);
            r.state = 2;
            if (trace) log.push_back("muzzle slot " + std::to_string(a.slot) + " " + std::to_string(r.muzzle[0]) + " " + std::to_string(r.muzzle[1]) + " " + std::to_string(r.muzzle[2]));
        }
    for (MovCol& d : a.mov_cols)
        if (d.active && d.joint >= 0) world(d.joint, d.pos);
}

// BattleSubWeaponDmg / SwordDmgCheck1: the weapon wears down with every hit (not the character's default weapon); when it is worn out it is replaced by the
// default weapon or by the next better weapon the character carries, and the worn out one is gone.
// Runs the code that follows with a remote player's record and position in place of the host's (his sphere hit a monster, his gold lay on the floor ...).
// When it ends the record is swapped back, what the code changed in it is reported as patches, and the events it made are tagged with the player.
struct RemoteScope {
    MonsterWorld& w;
    int id = 0;
    size_t first_event;
    std::unique_ptr<UserStatus> before;
    RemoteScope(MonsterWorld& world, int remote_id) : w(world), first_event(world.events.size()) {
        RemotePlayer* r = w.remote(remote_id);
        if (!r) return;
        id = remote_id;
        std::swap(w.status, r->status);
        std::swap(w.player, r->info);
        before = std::make_unique<UserStatus>(w.status);
    }
    ~RemoteScope() {
        if (!id) return;
        RemotePlayer* r = w.remote(id);
        const size_t n = UserStatus::kSize;
        size_t i = 0;
        while (i < n) {
            if (w.status.raw[i] == before->raw[i]) { ++i; continue; }
            size_t j = i + 1, last = i;
            while (j < n && j - last < 8) { if (w.status.raw[j] != before->raw[j]) last = j; ++j; }   // (close changes go in one patch)
            StatusPatch p;
            p.remote = id;
            p.offset = uint32_t(i);
            p.bytes.assign(w.status.raw.begin() + long(i), w.status.raw.begin() + long(last) + 1);
            w.status_patches.push_back(std::move(p));
            i = last + 1;
        }
        for (size_t k = first_event; k < w.events.size(); ++k) w.events[k].remote = id;
        std::swap(w.status, r->status);
        std::swap(w.player, r->info);
    }
};

void MonsterWorld::sword_dmg_check(float param, int wear) {
    const int ch = std::clamp(status.character(), 0, 5);
    const size_t base = 0x450c + size_t(ch) * 0xaa8;
    const size_t woff = status.weapon_offset(ch);
    const WeaponView w = status.weapon(ch);
    const int default_id = int(kDefaultWeapon[size_t(ch)]);
    if (w.id() == 0x10c) return;   // (the weapon of a story event; wears only once a game flag is set)
    const uint16_t wf = uint16_t(w.status_flags());
    if ((wf & 0x200) != 0) param = fmul(param, 0.5f);
    if ((wf & 0x100) != 0) param = fmul(param, 2.0f);
    if (w.id() == default_id) return;
    const float dur = w.durability();
    const float wear_step = fadd(fmul(fsub(1.5f, fmul(0.009999999776482582f, itof(w.s16(6)))), param), fmul(0.10000000149011612f, itof(wear)));
    float nd = fsub(dur, wear_step);
    status.set<float>(woff + 0x10, nd);
    const int max_dur = w.max_durability();
    if (nd <= 0.0f) {   // (an item in the active slots, 0xb7, repairs the weapon instead)
        for (int slot = 0; slot < 3; ++slot) {
            if (status.get<int16_t>(0x4362 + size_t(slot) * 2) == 0xb7) {
                status.set<int16_t>(0x4362 + size_t(slot) * 2, -1);
                nd = itof(max_dur);
                status.set<float>(woff + 0x10, nd);
                events.push_back({CombatEvent::WeaponRepaired, -1, {0, 0, 0}, w.id(), 0});
                break;
            }
        }
    }
    if (nd <= 0.0f) {
        status.set<float>(woff + 0x10, 0.0f);
        const int broken = w.id();
        if (default_id + 1 == broken) {   // the first weapon wears out into the default one
            if (item_db) item_db->make_weapon(default_id, status.raw.data() + woff, *rng_);
            else status.set<int16_t>(woff, int16_t(default_id));
            status.set<float>(woff + 0x10, 0.0f);
            events.push_back({CombatEvent::WeaponBroke, -1, {0, 0, 0}, broken, default_id});
            return;
        }
        for (int slot = 0; slot < 10; ++slot) {
            const size_t so = base + size_t(slot) * 0xf8;
            const int id = status.get<int16_t>(so);
            if (id == default_id || id == default_id + 1) {
                status.set<int8_t>(0x4340 + size_t(ch), int8_t(slot));
                status.set<int16_t>(woff, int16_t(-1));
                events.push_back({CombatEvent::WeaponBroke, -1, {0, 0, 0}, broken, id});
                return;
            }
        }
        return;
    }
    const float warn = fmul(0.10000000149011612f, itof(max_dur));
    if (warn <= dur && nd < warn) events.push_back({CombatEvent::WeaponWarn, -1, {0, 0, 0}, w.id(), 0});
}

void MonsterWorld::load_player_fx(const ShotDef& def, std::shared_ptr<Character> model, int index) {
    player_fx = shots.entry(1000 + index, def, std::move(model));
}

// CSHOT_EFFECT::Set + SetVsMonster / SetWepStatus / SetAttribute / SetDmg / SetWait with the weapon in use.
int MonsterWorld::fire_player_shot(const float pos[3], const float target[3], int owner_kind, int sub_id, int damage, int wait, int force_phase, bool set_attribute, bool set_damage,
                                   int loop, float random_rate, bool no_sound, bool attach) {
    if (player_fx < 0) return -1;
    ShotEffect& fx = shots.slot[size_t(player_fx)];
    const int i = fx.set(pos, target, owner_kind, sub_id, force_phase);
    if (i < 0) return -1;
    ShotEffect::Instance& s = fx.inst[size_t(i)];
    s.loop = loop;
    s.random_rate = random_rate;
    s.no_sound = no_sound;
    s.attach = attach;
    if (!set_damage) return i;
    const int ch = status.character();
    const WeaponView w = status.weapon(ch);
    static constexpr int kElement[6] = {1, 2, 4, 8, 0x10, 0};   // element_tbl (GetWeaponElementAttr)
    int choice = w.element_choice();
    if (choice < 0 || 5 < choice) choice = 5;
    s.slayer = w.slayer();
    s.status_flags = w.status_flags();
    if (set_attribute) fx.def.attr = kElement[choice];
    s.damage = damage;
    s.wait = wait;
    s.wait_counter = 0;
    return i;
}

void MonsterWorld::end_player_shot() {
    if (player_fx >= 0) shots.slot[size_t(player_fx)].end_all();
}

void MonsterWorld::step_effects() {
    step_thrown();
    for (CombatEvent& ev : events) {
        if (ev.processed) continue;
        ev.processed = true;
        if (ev.kind == CombatEvent::DropGold) drops.set(ev.pos, ev.value, -1);
        else if (ev.kind == CombatEvent::DropItem) drops.set(ev.pos, -1, ev.value);
        else if (ev.kind == CombatEvent::WeaponExp && item_db) {
            std::optional<RemoteScope> remote_scope;
            if (ev.remote > 0) remote_scope.emplace(*this, ev.remote);
            const int ch = ev.value2;
            const size_t woff = status.weapon_offset(ch);
            const int max = item_db->weapon_max_exp(status.raw.data() + woff);
            int16_t exp = status.get<int16_t>(woff + 0x14);
            if (status.get<int32_t>(0x8b10) == 10) {
                exp = int16_t(exp - ev.value);
                if (exp < 1) exp = 0;
            } else if (exp < max) {
                const int sum = exp + ev.value;
                exp = sum < max ? int16_t(sum) : int16_t(max);
            }
            status.set<int16_t>(woff + 0x14, exp);
        }
    }
    if (!coop_client) {   // (a co-op client's drops are the host's: its pick-ups come back as patches of the record)
    const int gold = drops.check_position(player.pos, status);
    const int total = int(status.get<uint16_t>(0x4346)) + gold;
    status.set<uint16_t>(0x4346, uint16_t(total < 0xffff ? total : 0xffff));
    if (item_db)
        for (int id : drops.take_picked()) get_item(status, *item_db, id, *rng_);
    }
    for (size_t ri = 0; ri < remotes.size(); ++ri) {   // co-op: the other players' pick-ups (their gold and items go into their own records)
        if (!remotes[ri]) continue;
        RemoteScope scope(*this, int(ri) + 1);
        const int rgold = drops.check_position(player.pos, status);
        const int rtotal = int(status.get<uint16_t>(0x4346)) + rgold;
        status.set<uint16_t>(0x4346, uint16_t(rtotal < 0xffff ? rtotal : 0xffff));
        if (item_db)
            for (int id : drops.take_picked()) get_item(status, *item_db, id, *rng_);
    }
    if (!coop_client) drops.step();
    // ... then the monsters' shot effects step (after the drops, bombs and the shock wave)
    ShotEnv env = shot_env();
    if (!coop_client) shots.step(env, ShotSystem::Which::Except, player_fx);   // (a co-op client's monster shots come from the host)
    step_mg();
    step_firebar();
    step_player_shots();   // NowMainEffect and the slingshot bullets follow the monsters' shot effects
}

ShotEnv MonsterWorld::shot_env() {
    ShotEnv env;
    env.pool = &pool;
    env.player_pos = player.prev_pos;
    env.collide = [this](float radius, const float pos[3], const float vel[3], int target, float hit[3]) { return shot_collide(radius, pos, vel, target, hit); };
    env.on_bomb = [this](float scale, const float pos[3], int target, int damage) {
        const int i = pool.set(fmul(scale, 20.0f), 0.0f, pos, damage, ftoi(fmul(scale, 45.0f)), target, 3, 0, 0);
        if (i != -1) { pool.e[size_t(i)].hit_counter = 10; pool.e[size_t(i)].hit_reset = 10; }
    };
    return env;
}

void MonsterWorld::trap_bomb(const float pos[3]) {
    const ShotEnv env = shot_env();
    env.on_bomb(1.0f, pos, 1, (floor_map + 1) * 20);
}

void MonsterWorld::trap_hurt(const float pos[3]) { pool.set(5.0f, 0.0f, pos, 2, 2, 1, 2, 0, 0); }

// The character's own effect (NowMainEffect) steps in the update phase, before the monsters are drawn.
void MonsterWorld::step_player_shots() {
    if (player_fx >= 0) {
        ShotEnv env = shot_env();
        shots.step(env, ShotSystem::Which::Only, player_fx);
    }
    step_sling();
}

void MonsterWorld::fire_sling(const float pos[3], const float vel[3], int damage) {
    for (SlingBullet& b : bullets) {
        if (b.active) continue;
        b = SlingBullet{};
        for (int k = 0; k < 3; ++k) { b.pos[k] = pos[k]; b.vel[k] = vel[k]; }
        b.life = 0x78;
        b.damage = damage;
        b.active = true;
        return;
    }
}

void MonsterWorld::fire_mg(const float pos[3], const float vel[3], int damage) {
    for (MgBullet& b : mg) {
        if (b.state != 0) continue;
        b = MgBullet{};
        for (int k = 0; k < 3; ++k) { b.pos[k] = pos[k]; b.vel[k] = vel[k]; }
        b.damage = damage;
        b.state = 1;
        return;
    }
}

// Step__15CSHOT_MACHINGUN
void MonsterWorld::step_mg() {
    for (MgBullet& b : mg) {
        if (b.state <= 0) continue;
        ++b.state;
        if (b.state >= 0xf0) {
            b.state = 0;
            continue;
        }
        float hit[3];
        const int h = shot_collide(2.0f, b.pos, b.vel, 2, hit);
        if (h == 1) {
            b.state = 0;   // (a spark shows where it struck the wall)
        }
        if (h == 3) {
            pool.source = 1;
            const int i = pool.set(4.0f, 1.0f, hit, b.damage, 2, 2, 2, 0, 0);
            pool.source = 0;
            if (i != -1) {
                ColEntry& e = pool.e[size_t(i)];
                const WeaponView w = status.weapon(status.character());
                static constexpr int kElement[6] = {1, 2, 4, 8, 0x10, 0};
                int choice = w.element_choice();
                if (choice < 0 || 5 < choice) choice = 5;
                e.flags = kElement[choice];
                e.slayer = w.slayer();
                e.status_flags = w.status_flags();
                e.owner_kind = 5;
                e.sub_id = 6;
            }
            b.state = 0;
        }
        for (int k = 0; k < 3; ++k) b.pos[k] = fadd(b.pos[k], b.vel[k]);
    }
}

// Init__13CSHOT_FIREBAR: the 24 blobs line up in front of the weapon
void MonsterWorld::firebar_init(const float pos[3], const float dir_in[3]) {
    float dir[4] = {dir_in[0], dir_in[1], dir_in[2], 1.0f};
    const vu0::V3 n = vu0::normalize(dir);
    for (int k = 0; k < 3; ++k) dir[k] = n[k];
    const float s[3] = {fmul(2.0f, dir[0]), fmul(2.0f, dir[1]), fmul(2.0f, dir[2])};
    for (int i = 0; i < 24; ++i) {
        FlameBlob& b = flame[size_t(i)];
        const float f = itof(i);
        b.pos[0] = fadd(pos[0], fmul(s[0], f));
        b.pos[1] = fadd(pos[1], fmul(s[1], f));
        b.pos[2] = fadd(pos[2], fmul(s[2], f));
        b.pos[3] = 1.0f;
        for (int k = 0; k < 3; ++k) b.vel[k] = fmul(0.009999999776482582f, dir[k]);
        b.size = fadd(fmul(0.30000001192092896f, f), 3.0f);
        b.life = fsub(180.0f, fmul(f, 8.0f));
        b.state = 0;
    }
}

// Set__13CSHOT_FIREBAR: every tick the blobs are aimed again at where they should be (one rand() draw each)
void MonsterWorld::firebar_set(const float pos[3], const float dir_in[3], int damage, int element) {
    float dir[4] = {dir_in[0], dir_in[1], dir_in[2], 1.0f};
    const vu0::V3 n = vu0::normalize(dir);
    for (int k = 0; k < 3; ++k) dir[k] = n[k];
    const float s[3] = {fmul(2.0f, dir[0]), fmul(2.0f, dir[1]), fmul(2.0f, dir[2])};
    for (int i = 0; i < 24; ++i) {
        FlameBlob& b = flame[size_t(i)];
        const float f = itof(i);
        const float d = fadd(fmul(f, 0.5f), 1.0f);
        for (int k = 0; k < 3; ++k) b.vel[k] = fdiv(fsub(fadd(pos[k], fmul(s[k], f)), b.pos[k]), d);
        b.state = 0;
        const int32_t r = rng_->next();
        b.size = fadd(fadd(fmul(0.30000001192092896f, f), 3.0f), fdiv(fmul(itof(r), 3.0f), kRandRange));
        b.life = fsub(180.0f, fmul(f, 8.0f));
        b.damage = damage;
        b.element = element;
    }
}

void MonsterWorld::firebar_reset() {
    for (FlameBlob& b : flame) b.state = -1;
}

// Step__13CSHOT_FIREBAR
void MonsterWorld::step_firebar() {
    if (++flame_cnt > 29) flame_cnt = 0;
    for (FlameBlob& b : flame) {
        if (b.state != 0) continue;
        b.life = fsub(b.life, 4.0f);
        b.size = fadd(b.size, 0.05999999865889549f);
        if (flame_cnt == 0) {
            pool.source = 1;
            const int i = pool.set(4.0f, 1.0f, b.pos, b.damage, 2, 2, 2, 0, 0);
            pool.source = 0;
            if (i != -1) {
                ColEntry& e = pool.e[size_t(i)];
                const WeaponView w = status.weapon(status.character());
                static constexpr int kElement[6] = {1, 2, 4, 8, 0x10, 0};
                int choice = w.element_choice();
                if (choice < 0 || 5 < choice) choice = 5;
                e.owner_kind = 5;
                e.sub_id = 6;
                e.status_flags = w.status_flags();
                e.slayer = w.slayer();
                e.flags = kElement[choice];
            }
        }
        for (int k = 0; k < 3; ++k) b.pos[k] = fadd(b.pos[k], b.vel[k]);
        if (b.life <= 32.0f) b.state = -1;
    }
}

// step__5CSHOT
void MonsterWorld::step_sling() {
    for (SlingBullet& b : bullets) {
        if (!b.active) continue;
        float hit[3];
        const int h = shot_collide(2.0f, b.pos, b.vel, 2, hit);
        if (h == 0) {
            b.pos[0] = fadd(b.pos[0], b.vel[0]);
            b.pos[1] = fadd(b.pos[1], b.vel[1]);
            b.pos[2] = fadd(b.pos[2], b.vel[2]);
        } else {
            pool.source = 1;
            const int i = pool.set(3.0f, 0.0f, b.pos, b.damage, 1, 2, 2, 0, 0);
            pool.source = 0;
            if (i != -1) {
                ColEntry& e = pool.e[size_t(i)];
                const WeaponView w = status.weapon(status.character());
                static constexpr int kElement[6] = {1, 2, 4, 8, 0x10, 0};
                int choice = w.element_choice();
                if (choice < 0 || 5 < choice) choice = 5;
                e.owner_kind = 1;
                e.sub_id = 0;
                e.flags = kElement[choice];
                e.status_flags = w.status_flags();
                e.slayer = w.slayer();
            }
            b.active = false;
        }
        if (--b.life < 1) b.active = false;
    }
}

// The rand() draws of the visual effects a hit starts (they change the game's random sequence for everything that rolls afterwards in the frame).
void MonsterWorld::hit_effect_draws(int element) {
    for (int i = 0; i < 224; ++i) rng_->next();   // CHitMark::Set: 32 particles x 7
    if (element < 5) {
        const int ch = status.character();
        const WeaponView w = status.weapon(ch);
        const float pts = itof(w.element_value(w.element_choice()));
        const float f = fmul(0.009999999776482582f, fadd(pts, 1.0f));
        int n = 0, per = 0, extra = 0;
        switch (element) {
            case 4: n = ftoi(fmul(f, 12.0f)) + 2; per = 6; break;                       // holy
            case 2: n = ftoi(fmul(f, 18.0f)) + 6; per = 9; extra = (ftoi(fmul(f, 7.0f)) + 1) * 4; break;   // thunder
            case 0: n = ftoi(fmul(f, 12.0f)) + 2; per = 6; break;                       // fire
            case 3: n = ftoi(fmul(f, 10.0f)) + 1; per = 9; break;                       // wind
            default: n = ftoi(fmul(f, 12.0f)) + 2; per = 6; break;                      // cold
        }
        for (int i = 0; i < n * per + extra; ++i) rng_->next();
    }
}

int MonsterWorld::check_dmg(MonsterActor& a) {
    int result = 0;                                   // iVar11 at the start
    const int slot = a.slot;
    const float mpos[3] = {a.pos[0], a.pos[1], a.pos[2]};
    // 1. the monster's own attack spheres go into the pool
    if (a.hit_stop < 1) {
        for (int i = 0; i < 16; ++i) {
            DmgCol& d = a.dmg_cols[size_t(i)];
            if (!d.active || !(d.win_start < a.motion.time && a.motion.time < d.win_end)) continue;
            int damage = d.damage;
            if (0 < a.bin2) damage <<= 1;
            const int idx = pool.set(d.radius, 0.0f, d.pos, damage, 2, 1, d.type, d.flags, 0);
            if (idx < 0) continue;
            ColEntry& e = pool.e[size_t(pool.last)];
            e.owner_kind = slot * 5 + 200;
            e.sub_id = i;
            if (d.type == 3) {
                if (d.angle == 0.0f) {
                    float v[3] = {fsub(player.pos[0], mpos[0]), 0.0f, fsub(player.pos[2], mpos[2])};
                    vu_normalize(v, v);
                    vu_normalize(v, v);
                    e.dir[0] = v[0]; e.dir[1] = v[1]; e.dir[2] = v[2];
                } else {
                    float ang = d.angle;
                    if (180.0f <= ang) ang = fsub(ang, 360.0f);
                    ang = fmul(kDegToRad, ang);
                    ang = fadd(ang, a.rot[1]);
                    if (kTwoPiF < ang) ang = fsub(ang, kTwoPiF);
                    if (ang < -kPiF) ang = fadd(ang, kTwoPiF);
                    const float sn = ps2_sinf(ang), cs = ps2_cosf(ang);
                    float v[3] = {sn, 0.0f, cs};   // (0, 0, 1) turned about Y
                    vu_normalize(v, v);
                    e.dir[0] = v[0]; e.dir[1] = v[1]; e.dir[2] = v[2];
                }
            }
        }
    }
    // 2. nothing more to do for a monster without life
    if (a.hp < 1) {
        if (0 < a.hit_stop) {
            a.hit_stop = 0;
            a.motion_restore = true;
        }
        return 0;
    }
    a.dmg_id = -1;
    if (0 < a.hit_stop) {
        --a.hit_stop;
        if (a.hit_stop == 0) a.motion_restore = true;
    }
    if (0 < a.bin2) --a.bin2;
    if (0 < a.poison) {
        --a.poison;
        if (a.poison == 0) {
            a.poison = 0xb4;
            const float loss = fmul(0.1f, itof(a.hp_max));
            const int lost = ftoi(loss);
            a.hp -= lost;
            result = 1;
            if (a.hp < 1) {
                a.hp = 0;
                --alive;
                result = 2;
                status.add_kill();
            }
            events.push_back({CombatEvent::Poison, slot, {mpos[0], mpos[1], mpos[2]}, lost, 0});
        }
    }
    // 3. attacks of the player (and of other monsters' shots) in the pool
    if (a.muteki >= 1) return result;
    for (int bc = 0; bc < 16; ++bc) {
        BodyCol& body = a.body_cols[size_t(bc)];
        if (!body.active) continue;
        bool usable = true;
        if (body.win_start != 0.0f && (a.motion.time <= body.win_start || body.win_end < a.motion.time)) usable = false;
        if (!usable) continue;
        int j = -1;
        for (int k = 0; k < ColPool::kSize; ++k) {
            const ColEntry& e = pool.e[size_t(k)];
            if (!e.used || (e.target & 2) == 0 || e.hit_counter != e.hit_reset) continue;
            if (vu_dist(body.pos, e.pos) <= fadd(body.radius, e.radius)) { j = k; break; }
        }
        bool handled = false;
        std::optional<RemoteScope> remote_scope;   // (co-op: the attacker is another player: his record and position are in force until the end of this round)
        if (j != -1 && pool.e[size_t(j)].remote > 0) remote_scope.emplace(*this, pool.e[size_t(j)].remote);
        if (j != -1) {
            ColEntry& e = pool.e[size_t(j)];
            handled = e.owner_id == slot;
            if (e.owner_id != -1 && e.owner_id != slot) {   // a shot fired by another monster
                const float roll = percent_roll(*rng_);
                if ((e.flags & 0x1000) != 0 && roll < itof(a.status_chance) && a.bin2 == 0) {
                    a.bin2 = 0x708;
                    a.poison = 0;
                    a.status_slow = 0;
                }
                handled = true;
            }
        }
        if (j != -1 && !handled) {   // guard frames: the monster blocks
            ColEntry& e = pool.e[size_t(j)];
            for (int g = 0; g < 3; ++g) {
                if (a.guard_on[size_t(g)] == 0) continue;
                if (a.guard_start[size_t(g)] <= a.motion.time && a.motion.time <= a.guard_end[size_t(g)]) {
                    if (e.kick_mode == 2) {
                        float v[3] = {fsub(mpos[0], e.kick_pos[0]), 0.0f, fsub(mpos[2], e.kick_pos[2])};
                        vu_normalize(v, v);
                        a.push_dir[0] = v[0]; a.push_dir[1] = v[1]; a.push_dir[2] = v[2]; a.push_dir[3] = 0.0f;
                        a.push_speed = fmul(fmul(e.kick_speed, 1.5f), a.push_scale);
                        a.push_decay = fmul(fmul(e.kick_decay, 1.5f), a.push_scale);
                    }
                    if (e.sub_id == 0 && (e.owner_kind == 0 || e.owner_kind == 2 || e.owner_kind == 4)) sword_dmg_check(0.10000000149011612f, a.weapon_wear);
                    hit_effect_draws(5);
                    events.push_back({CombatEvent::Blocked, slot, {mpos[0], mpos[1], mpos[2]}, 0, 0});
                    handled = true;
                    break;
                }
            }
        }
        if (j == -1 || handled) continue;
        // --- the hit lands
        ColEntry& e = pool.e[size_t(j)];
        const int owner = e.owner_kind;
        a.hit_body_col = bc;
        int dmg_id = -1;
        if (owner != -1) dmg_id = owner * 10;
        if (e.sub_id != -1) dmg_id += e.sub_id;
        a.dmg_id = dmg_id;
        a.hit_status = e.status_flags;
        const int flags = e.flags;
        const int elem = element_index(flags);
        a.element_hit = elem;
        if (a.steal_item != -1 && (a.hit_status & 0x80) != 0) {
            const int pct = ftoi(percent_roll(*rng_));
            if (pct < 10 && check_item_get(a.steal_item) == 0) {
                events.push_back({CombatEvent::Steal, slot, {mpos[0], mpos[1] + 12.0f, mpos[2]}, a.steal_item, 0});
                a.steal_item = -1;
            }
        }
        int mode = 1;   // iVar14: 0 = no flinch, 1 = flinch, 2 = dead
        const bool special_floor = (floor_map == 0 && status.floor() == 0x0e) || (floor_map == 5 && status.floor() == 0x18);
        if (!special_floor) {
            if (owner == 5 && e.sub_id == 6) mode = 0;
            if (owner == 1) mode = 0;
        }
        if (e.kick_mode == 2) {
            float v[3] = {fsub(mpos[0], e.kick_pos[0]), 0.0f, fsub(mpos[2], e.kick_pos[2])};
            vu_normalize(v, v);
            a.push_dir[0] = v[0]; a.push_dir[1] = v[1]; a.push_dir[2] = v[2]; a.push_dir[3] = 0.0f;
            a.push_speed = fmul(e.kick_speed, a.push_scale);
            a.push_decay = fmul(e.kick_decay, a.push_scale);
        }
        if (owner == 0 || owner == 2 || owner == 4) sword_dmg_check(1.0f, a.weapon_wear);
        hit_effect_draws(elem);
        float roll = percent_roll(*rng_);
        bool miss_flash = false;
        if ((a.hit_status & 0x20) != 0) {
            const float r2 = percent_roll(*rng_);
            if (10.0f < r2 || itof(a.status_chance) <= roll) {
                if (owner == -1) miss_flash = true;
            } else {
                a.poison = 0xb4;
                a.status_slow = 0;
            }
        }
        if ((a.hit_status & 0x40) != 0) {
            const float r2 = percent_roll(*rng_);
            if (4.0f < r2 || itof(a.status_chance) <= roll) {
                if (owner == -1) miss_flash = true;
            } else if (a.hit_stop < 1) {
                a.hit_stop = 300;
                a.poison = 0;
                a.status_slow = 0;
                a.bin2 = 0;
                a.move_speed = 0.0f;
            } else {
                a.hit_stop = 0;
            }
        }
        const int16_t chance = a.status_chance;
        roll = chance == 0 ? 100.0f : 0.0f;   // from here on the game compares against 0 or 100, not a random roll
        if ((flags & 0x200) != 0) {
            if (roll < itof(chance)) {
                if (a.hit_stop == 0 && a.bin2 == 0) {
                    a.poison = 0xb4;
                    a.status_slow = 0;
                }
            } else if (owner == -1) {
                miss_flash = true;
            }
        }
        if ((flags & 0x100) != 0) {
            if (roll < itof(chance)) {
                if (a.hit_stop < 1) {
                    a.hit_stop = 300;
                    a.poison = 0;
                    a.status_slow = 0;
                    a.bin2 = 0;
                    a.move_speed = 0.0f;
                } else {
                    a.hit_stop = 0;
                }
            } else if (owner == -1) {
                miss_flash = true;
            }
        }
        if ((flags & 0x800) != 0) {
            if (roll < itof(chance)) {
                if (a.hit_stop == 0 && a.poison == 0 && a.bin2 == 0) a.status_slow = 0xb4;
            } else if (owner == -1) {
                miss_flash = true;
            }
        }
        // --- damage
        float dmg = itof(e.damage);
        if (owner == 1 || owner == 3 || owner == 5) {   // ranged weapons: stronger close up, weaker far away
            const float d = vu_dist(player.pos, e.pos);
            if (d <= 20.0f) dmg = fmul(dmg, 1.5f);
            if (50.0f <= d) {
                const float f20 = fdiv(fsub(100.0f, fsub(d, 50.0f)), 100.0f);
                float f19 = 0.5f;
                if (0.5f <= f20) f19 = f20;
                dmg = fmul(dmg, f19);
            }
        }
        float def = itof(a.defense);
        if (owner == 3) def = fdiv(def, 2.0f);
        dmg = fsub(dmg, def);
        if (dmg <= 0.0f) dmg = 1.0f;
        float scaled_dmg = dmg;
        if (flags != 0) {
            float bonus = 0.0f;
            if (owner >= 0 && owner <= 5) {
                const WeaponView w = status.weapon(owner);
                bonus = fmul(dmg, fadd(fmul(0.004000000189989805f, itof(w.element_value(elem))), fmul(0.004999999888241291f, itof(w.magic()))));
            }
            const int16_t resist = elem < 5 ? a.resist[size_t(elem)] : int16_t(a.gold & 0xffff);   // (the game reads past the table for effects without an element)
            scaled_dmg = fmul(fmul(fadd(dmg, bonus), 0.009999999776482582f), itof(resist));
            if (scaled_dmg <= 0.0f && 0.0f < dmg) miss_flash = true;
        }
        if (e.slayer != nullptr) scaled_dmg = fadd(scaled_dmg, fmul(fmul(scaled_dmg, 0.014999999664723873f), itof(e.slayer[a.monster_code])));
        float final_dmg = scaled_dmg;
        if (owner != -1) {
            final_dmg = fmul(fdiv(scaled_dmg, 100.0f), itof(owner >= 0 && owner <= 5 ? body.attacker_pct[size_t(owner)] : 100));
            if (final_dmg <= 0.0f) final_dmg = 0.0f;
            if (final_dmg <= 0.0f && 0.0f < scaled_dmg) miss_flash = true;
        }
        if (e.only_monster_kind != -1 && a.monster_code != e.only_monster_kind) {
            final_dmg = 0.0f;
            miss_flash = true;
        }
        if (0 < a.bin2) final_dmg = fdiv(final_dmg, 2.0f);
        if (owner == -1) final_dmg = fmul(fmul(final_dmg, 0.009999999776482582f), itof(a.dmg_scale));
        if (final_dmg <= 0.0f) final_dmg = 0.0f;
        int amount = ceil_to_int(final_dmg);
        if ((a.hit_status & 0x400) != 0 && 100.0f <= final_dmg && owner >= 0 && 0 < status.life(owner)) {
            const int heal = ftoi(fmul(0.009999999776482582f, final_dmg));
            const int16_t now = status.life(owner), top = status.max_life(owner);
            status.set_life(owner, int16_t(std::min<int>(now + heal, top)));   // AddNowLife
        }
        if ((a.hit_status & 0x1000) != 0 && a.kind != 2) {
            if (percent_roll(*rng_) < 1.0f) amount = a.hp;
        }
        last_attacker = owner;
        a.hp -= amount;
        if (a.hp < 1) {
            a.killer = owner;
            a.hp = 0;
            --alive;
            mode = 2;
            status.add_kill();
            if (a.death_event != -1) a.death_event_pending = 1;
        }
        events.push_back({miss_flash ? CombatEvent::Blocked : CombatEvent::Hit, slot, {mpos[0], mpos[1], mpos[2]}, amount, owner});
        if (mode == 2) events.push_back({CombatEvent::Death, slot, {mpos[0], mpos[1], mpos[2]}, owner, 0});
        if (e.target != 3) e.used = false;
        return mode;
    }
    return result;
}

int MonsterWorld::check_item_get(int item) const {
    // CDngStatusData::CheckItemGet: the player already holds as many of this kind of item as can be carried
    if (uint32_t(item - 0xe9) < 2 || item == 0xee) return 0;
    if (item > 0x83 && item < 0x101) {
        int count = 0;
        for (int i = 0; i < 100; ++i) {
            const int16_t v = status.get<int16_t>(0x436e + size_t(i) * 2);
            if (v > 0x83 && v < 0x101) ++count;
        }
        for (int i = 0; i < 3; ++i)
            if (status.get<int16_t>(0x4362 + size_t(i) * 2) != -1) count += status.get<int16_t>(0x4368 + size_t(i) * 2);
        if (status.get<int8_t>(0x4360) <= count) return 1;
    }
    if (item > 0x50 && item < 0x84) {
        int count = 0;
        for (int i = 0; i < 0x28; ++i) {
            const int16_t v = status.get<int16_t>(0x84fc + size_t(i) * 0x20);
            if (v > 0x50 && v < 0x84) ++count;
        }
        return count < 0x28 ? 0 : 2;
    }
    return 0;
}

int MonsterWorld::select_attach(const MonsterActor& a) {
    // SelectAttachi: which "attach" item (synthesis material) a monster may leave behind, or -1
    if (!(ftoi(percent_roll(*rng_)) < 0x47)) return -1;
    const int pct = ftoi(percent_roll(*rng_));
    bool better = false;
    int best = 0;
    for (int k = 1; k < 5; ++k)
        if (a.resist[size_t(best)] < a.resist[size_t(k)]) {
            better = true;
            best = k;
        }
    if (pct < 0x1e && better) {
        const int id = best + 0x51;
        return (id < 0x51 || 0x55 < id) ? -1 : id;
    }
    const int id = a.monster_code + 0x6f;
    return (id < 0x6f || 0x78 < id) ? -1 : id;
}

void MonsterWorld::set_key(int map_no, int floor, int map1_variant) {
    if (double_weapon_exp || (map_no == 2 && floor == 0x10)) return;
    int to_place = map_no == 1 ? 3 : 1;
    if (alive <= 1) return;
    int key_index = 0, attempts = 0;
    for (;;) {
        int idx;
        do {
            idx = gm::scaled(alive, rng_->next());
            if (idx < 0 || alive <= idx) idx = 0;
        } while (actors[size_t(idx)].slot < 0);
        MonsterActor& a = actors[size_t(idx)];
        int key;
        switch (map_no) {
            case 0: key = 0xc3; break;
            case 1: { constexpr int k[3] = {196, 198, 205}; key = k[std::clamp(key_index, 0, 2)]; (void)map1_variant; break; }
            case 2: key = 0xc9; break;
            case 3: key = 0xca; break;
            case 4: key = 0xcb; break;
            case 5: key = 0xcc; break;
            case 6: key = 0xce; break;
            default: key = -1;
        }
        if (a.dropped_item == -1 && a.drops_enabled != 0) {
            bool in_bag = false;
            for (int i = 0; i < 0x67; ++i)
                if (status.get<int16_t>(0x436e + size_t(i) * 2) == key) in_bag = true;
            bool on_floor = false;
            for (const Drop& d : drops.d)
                if (d.used && d.item == key) on_floor = true;
            if (!in_bag && !on_floor) a.dropped_item = int16_t(key);
            ++key_index;
            if (--to_place < 1) return;
        }
        if (++attempts > 0x270e) a.drops_enabled = 1;
    }
}

bool MonsterWorld::set_gate_key(int item) {
    if (item == -1) return false;
    for (int v : gate_key_stack)
        if (v == item) return false;
    for (int& v : gate_key_stack)
        if (v == -1) {
            v = item;
            return true;
        }
    return false;
}

void MonsterWorld::drop_loot(MonsterActor& a) {
    // the part of CMonstorUnit::Step that runs when a monster has just died (state -1)
    const int ch = status.character();
    const float pos[3] = {a.pos[0], a.pos[1], a.pos[2]};
    const bool default_weapon = status.weapon(ch).id() == int(kDefaultWeapon[size_t(std::clamp(ch, 0, 5))]);
    if (a.killer == ch && !default_weapon) {
        int exp = a.exp;
        if (double_weapon_exp) exp <<= 1;
        if ((a.hit_status & 0x2000) != 0) exp = ftoi(fmul(itof(exp), 1.2000000476837158f));
        events.push_back({CombatEvent::WeaponExp, a.slot, {pos[0], pos[1], pos[2]}, exp, ch});
    }
    if (a.stolen_gold > 0) {
        events.push_back({CombatEvent::DropGold, a.slot, {pos[0], pos[1] - a.height, pos[2]}, a.stolen_gold, 0});
        a.drop_pct = 0;
    }
    if (a.kind != 2 && a.drops_enabled != 0) {
        if (a.dropped_item == -1 && a.rare_item != -1 && percent_roll(*rng_) < 10.0f) a.dropped_item = a.rare_item;
        if (a.dropped_item == -1) {
            const int attach = select_attach(a);
            if (a.killer == -1 && attach != -1) {
                events.push_back({CombatEvent::DropItem, a.slot, {pos[0], pos[1] - a.height, pos[2]}, attach, 0});
                a.dropped_item = int16_t(attach);
            } else {
                a.dropped_item = -1;
            }
        } else {
            if (set_gate_key(a.dropped_item)) events.push_back({CombatEvent::DropItem, a.slot, {pos[0], pos[1] - a.height, pos[2]}, a.dropped_item, 0});
        }
        if (a.dropped_item == -1) {
            const int pct = ftoi(percent_roll(*rng_));
            if (pct < a.drop_pct) {
                const int base_gold = a.gold;
                const int extra = ftoi(fdiv(fdiv(fmul(itof(base_gold), itof(rng_->next())), 2.0f), kRandRange));
                int gold = base_gold + extra;
                if ((a.hit_status & 2) != 0) gold *= 2;
                if ((a.hit_status & 4) != 0 && 1 < gold) gold = int(double(gold) * 0.5);
                events.push_back({CombatEvent::DropGold, a.slot, {pos[0], pos[1] - a.height, pos[2]}, gold, 0});
            }
        }
    }
}

}  // namespace dccore

namespace dccore {

int MonsterWorld::throw_item(int id, const float pos[3], const float vel[3]) {
    for (size_t i = 0; i < thrown.size(); ++i) {
        ThrownItem& t = thrown[i];
        if (t.active) continue;
        t = ThrownItem{};
        t.active = true;
        t.id = id;
        for (int k = 0; k < 3; ++k) { t.pos[k] = pos[k]; t.vel[k] = vel[k]; }
        if (trace) log.push_back("throw item " + std::to_string(id) + " from " + std::to_string(pos[0]) + "," + std::to_string(pos[1]) + "," + std::to_string(pos[2]) + " v " + std::to_string(vel[0]) + "," + std::to_string(vel[1]) + "," + std::to_string(vel[2]));
        return int(i);
    }
    return -1;
}

// The flight of the thrown items (ItemThrowStep) and what they do when they go off.
void MonsterWorld::step_thrown() {
    for (ThrownItem& t : thrown) {
        if (!t.active) continue;
        float* p = t.pos;
        float* v = t.vel;
        const float q[3] = {fadd(p[0], v[0]), fadd(p[1], v[1]), fadd(p[2], v[2])};
        v[1] = fsub(v[1], 0.11999999731779099f);
        int r = 0;
        for (const MonsterActor& o : actors) {   // the monsters' body spheres (the unit's own active flag is not asked)
            if (r != 0) break;
            for (const BodyCol& b : o.body_cols) {
                if (!b.active) continue;
                if (vu_dist(b.pos, p) <= fadd(b.radius, 1.5f)) { r = 2; break; }
            }
        }
        if (r == 0) {
            std::vector<CCPoly> polys;
            const float lo[3] = {fsub(p[0], 20.0f), fsub(p[1], 30.0f), fsub(p[2], 20.0f)}, hi[3] = {fadd(p[0], 20.0f), fadd(p[1], 30.0f), fadd(p[2], 20.0f)};
            for (const CCPoly& poly : floor_polys_near(*dungeon_, *layout_, p[0], p[2], 1)) {
                bool in = true;
                for (int i = 0; i < 3 && in; ++i) {
                    const float mn = std::min({poly.v[0][i], poly.v[1][i], poly.v[2][i]}), mx = std::max({poly.v[0][i], poly.v[1][i], poly.v[2][i]});
                    if (mx < lo[i] || mn > hi[i]) in = false;
                }
                if (in) polys.push_back(poly);
            }
            float hit[3];
            const int h = check_hit(polys.data(), int(polys.size()), p, q, hit, true, 4);
            if (h < 0) {
                for (int k = 0; k < 3; ++k) p[k] = q[k];
            } else {   // it bounces: the travelled segment is mirrored on the plane, a third of the speed stays
                float n3[3] = {polys[size_t(h)].n[0], polys[size_t(h)].n[1], polys[size_t(h)].n[2]};
                vu_normalize(n3, n3);
                const float d = fadd(fadd(fmul(n3[0], fsub(p[0], hit[0])), fmul(n3[1], fsub(p[1], hit[1]))), fmul(n3[2], fsub(p[2], hit[2])));
                for (int k = 0; k < 3; ++k) {
                    v[k] = fadd(fsub(hit[k], p[k]), fmul(fmul(2.0f, d), n3[k]));
                    p[k] = fadd(hit[k], fmul(1.2999999523162842f, n3[k]));
                    v[k] = fmul(v[k], 0.4000000059604645f);
                }
            }
        }
        t.n += 1;
        if (r == 2) t.n = 0x2d;
        if (t.n < 0x2d) continue;
        t.active = false;
        const int D = (floor_map + 1) * 30;
        const float at[3] = {p[0], p[1], p[2]};
        blasts.push_back({t.id, {at[0], at[1], at[2]}});
        if (trace) log.push_back("blast item " + std::to_string(t.id) + " at " + std::to_string(at[0]) + "," + std::to_string(at[1]) + "," + std::to_string(at[2]) + " damage " + std::to_string(D));
        switch (t.id) {
            case 0xa1: case 0xa2: case 0xa3: case 0xa4: case 0xa5: case 0x98: {   // the gems and the holy water: a burst of their element (a sphere stands in for the effect)
                static constexpr int kElem[5] = {1, 2, 4, 8, 0x10};
                const int i = pool.set(24.0f, 0.0f, at, D, 10, 2, 2, t.id == 0x98 ? 0x10 : kElem[t.id - 0xa1], 0);
                if (i != -1 && t.id == 0x98) pool.e[size_t(i)].only_monster_kind = 1;
                break;
            }
            case 0xa0: pool.set(8.0f, 1.0f, at, 8, 5, 2, 2, 0, 0); break;      // a stone
            case 0xa6: pool.set(8.0f, 1.0f, at, 2, 5, 2, 2, 0x100, 0); break;  // Throbbing Cherry: freezes
            case 0xa9: pool.set(8.0f, 1.0f, at, 2, 5, 2, 2, 0x200, 0); break;  // Poisonous Apple: poisons
            case 0xa7: pool.set(8.0f, 1.0f, at, 8, 5, 2, 2, 0x800, 0); break;  // Gooey Peach: slows
            default: {   // the bombs (SetBombEffect)
                const ShotEnv env = shot_env();
                env.on_bomb(1.0f, at, 3, D);
            }
        }
    }
}

}  // namespace dccore
