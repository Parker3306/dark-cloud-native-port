#include "dccore/monster_ai.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "dccore/gamemath.h"
#include "dccore/libm_ps2.h"
#include "dccore/vu0.h"
#include "dccore/motion.h"
#include "dccore/walk.h"

namespace dccore {

using namespace gm;

namespace {

constexpr float kPiF = 3.14159274101257324f;      // DAT_002a18a0
constexpr float kTwoPiF = 6.28318548202514648f;
constexpr float kDegToRad = 0.01745329238474369f; // DAT_002a1968
constexpr float kGravity = 0.2f;                  // DAT_002a1d0c
constexpr float kTiny = 9.999999747378752e-05f;   // DAT_002a1898
constexpr float kMotionEps = 0.009999999776482582f; // DAT_002a18bc
constexpr float kRotDone = 0.05235987901687622f;  // DAT_002a1d8c (3 degrees)

// libvu0 sceVu0Normalize: Q = 1 / sqrt((x*x + y*y) + z*z); xyz *= Q (w is not used here).
}  // namespace

float vu_dist(const float a[3], const float b[3]) { return vu0::dist(a, b); }

void vu_normalize(const float in[3], float out[3]) {
    const vu0::V3 r = vu0::normalize(in);
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2];
}

namespace {

// AngleCmp(a, b, tol): 0 when |a - b| (wrapped) <= tol, otherwise the sign of the difference.
int angle_cmp(float a, float b, float tol) {
    float d = fsub(a, b);
    if (d == 0.0f) return 0;
    if (kPiF < d) d = fsub(d, kTwoPiF);
    if (d < -kPiF) d = fadd(d, kTwoPiF);
    if (tol < d) return 1;
    if (d < -tol) return -1;
    return 0;
}

// (0, 0, 1) turned about Y by `yaw`: the direction a character with that rotation faces.
void facing(float yaw, float out[3]) {
    out[0] = ps2_sinf(yaw);
    out[1] = 0.0f;
    out[2] = ps2_cosf(yaw);
    vu_normalize(out, out);
}

void turn_vector(const float in[3], float yaw, float out[3]) {
    const float s = ps2_sinf(yaw), c = ps2_cosf(yaw);
    const float x = fadd(fmul(in[0], c), fmul(in[2], s));
    const float z = fsub(fmul(in[2], c), fmul(in[0], s));
    out[0] = x; out[2] = z;
}

}  // namespace

MonsterWorld::MonsterWorld(const Dungeon* dungeon, const FloorLayout* layout, const MonsterUnit* unit, GameRand* rng)
    : dungeon_(dungeon), layout_(layout), unit_(unit), rng_(rng) {}

void MonsterWorld::clear_monsters() {
    for (MonsterActor& a : actors) a = MonsterActor{};
    alive = 0;
    run_script_request = -1;
    for (int i = 0; i < ShotSystem::kSlots; ++i)
        if (i != player_fx) shots.slot[size_t(i)] = ShotEffect{};
    for (ColEntry& e : pool.e)
        if (e.used && e.owner_kind >= 200) e.used = false;
}

void MonsterWorld::spawn_all() {
    for (int i = 0; i < MonsterUnit::kSlots; ++i)
        if (unit_->slots[size_t(i)].active) spawn(i);
}

void MonsterWorld::spawn(int slot) {
    const MonsterInstance& inst = unit_->slots[size_t(slot)];
    const MonsterBase& base = unit_->bases[size_t(inst.base)];
    MonsterActor& a = actors[size_t(slot)];
    a = MonsterActor{};
    a.slot = slot;
    a.base = inst.base;
    std::copy(inst.pos, inst.pos + 3, a.pos);
    a.rot[1] = 0.0f;
    a.state = 1;
    a.hp_max = a.hp = base.entry.s32(0x50);
    if (coop_players > 1) a.hp_max = a.hp = std::max(1, int(float(a.hp) * coop_hp_mult(coop_players) + 0.5f));   // (co-op balance)
    a.monster_code = base.entry.s16(0x54);
    a.kind_boss = base.entry.kind() == 2;
    for (int i = 0; i < 5; ++i) a.resist[size_t(i)] = base.entry.s16(0x56 + size_t(i) * 2);
    a.defense = base.entry.s16(0x64);
    a.weapon_wear = base.entry.s16(0x66);
    a.gold = base.entry.s32(0x70);
    a.drop_pct = base.entry.s32(0x74);
    a.exp = base.entry.s16(0x6c);
    a.steal_item = base.entry.s16(0x80);
    a.drops_enabled = base.entry.s16(0x82);
    a.dmg_scale = base.entry.s16(0x84);
    a.status_chance = base.entry.s16(0x86);
    a.rare_item = base.entry.s16(0x88);
    a.push_scale = base.entry.f32(0x98);
    for (BodyCol& b : a.body_cols)
        for (int k = 0; k < 6; ++k) b.attacker_pct[size_t(k)] = base.entry.s16(0x8a + size_t(k) * 2);
    ++alive;
    for (int k = 0; k < 2; ++k)
        if (base.shot_def[size_t(k)] >= 0) a.shot_slot[size_t(k)] = shots.entry(base.shot_def[size_t(k)], base.shot[size_t(k)], base.shot_model[size_t(k)]);
    if (trace) log.push_back("spawn slot " + std::to_string(slot) + " shot defs " + std::to_string(base.shot_def[0]) + "," + std::to_string(base.shot_def[1]) + " slots " + std::to_string(a.shot_slot[0]) + "," + std::to_string(a.shot_slot[1]));
    a.kind = base.entry.s16(0x78);
    a.body_radius = a.mon_radius = base.entry.f32(0x60);
    a.active = base.entry.s16(0x54) == 8 ? int16_t(0) : int16_t(-1);
    if (base.model)
        for (const MotionEntry& m : base.model->motions) a.table.push_back({int(m.start), int(m.end), m.speed});
    if (!a.table.empty()) {
        a.motion.time = float(a.table[0].start);   // the template character starts at the first frame of motion 0
        a.motion.frame = a.table[0].start;
        a.motion.next = a.motion.frame + 1;
    }
    if (base.script) {
        a.vm.load(base.script);
        a.vm.set_external([this](int32_t id, const ScriptArgs& args) { return ext(id, args); });
        cur_ = slot;
        if (a.vm.has_function(1)) a.vm.run(1);
    }
    a.script_active = false;
}

void MonsterWorld::check_view_level() {
    int near_list[MonsterUnit::kSlots];
    int count = 0;
    for (auto& a : actors) {
        // (the game clears a per-monster "put to sleep" flag here)
        if (a.state == -1 || a.active == 0) continue;
        a.dist_to_player = vu_dist(player.pos, a.pos);
        for (const auto& r : remotes)   // co-op: the nearest of all players wakes the monster
            if (r) a.dist_to_player = std::min(a.dist_to_player, vu_dist(r->info.pos, a.pos));
        if (a.state == 1 && a.dist_to_player < a.clip_level) a.state = 2;
        if (a.state == 2 && fadd(a.clip_level, 10.0f) < a.dist_to_player) a.state = 1;
        if (a.hp < 1) a.state = 2;
        if (a.state == 2) near_list[count++] = a.slot;
    }
    if (count > 4 && !last_floor) {
        // keep the four nearest awake, put the rest back to sleep
        std::vector<int> order;
        std::vector<int> pool(near_list, near_list + count);
        for (int i = 0; i < count; ++i) {
            float best = 3200.0f;
            int pick = -1;
            for (int j = 0; j < count; ++j) {
                if (pool[size_t(j)] == -1) continue;
                const float d = actors[size_t(pool[size_t(j)])].dist_to_player;
                if (d < best) { best = d; pick = j; }
            }
            if (pick != -1) {
                order.push_back(pool[size_t(pick)]);
                pool[size_t(pick)] = -1;
            }
        }
        for (size_t i = 4; i < order.size(); ++i) {
            MonsterActor& a = actors[size_t(order[i])];
            if (a.state == 2 && a.hp > 0 && a.kind != 2) a.state = 1;
        }
    }
}

namespace {
bool overlaps(const CCPoly& q, const float lo[3], const float hi[3]) {
    for (int i = 0; i < 3; ++i) {
        const float mn = std::min({q.v[0][i], q.v[1][i], q.v[2][i]}), mx = std::max({q.v[0][i], q.v[1][i], q.v[2][i]});
        if (mx < lo[i] || mn > hi[i]) return false;
    }
    return true;
}
}  // namespace

// setCollisionData(30, 5): the floor polygons of the 3 x 3 cells around the monster that touch a box 30 units wide and 150 high around it; then (Step) the
// stand-in prisms of the other awake monsters that touch a box 30 wide and 80 high.
void MonsterWorld::gather_polys(const MonsterActor& a, bool with_monsters) {
    polys_.clear();
    const float lo[3] = {fsub(a.pos[0], 30.0f), fsub(a.pos[1], 150.0f), fsub(a.pos[2], 30.0f)}, hi[3] = {fadd(a.pos[0], 30.0f), fadd(a.pos[1], 150.0f), fadd(a.pos[2], 30.0f)};
    for (const CCPoly& q : floor_polys_near(*dungeon_, *layout_, a.pos[0], a.pos[2], 1))
        if (overlaps(q, lo, hi)) polys_.push_back(q);
    terrain_count_ = int(polys_.size());
    if (!with_monsters) return;
    const float lo2[3] = {lo[0], fsub(a.pos[1], 80.0f), lo[2]}, hi2[3] = {hi[0], fadd(a.pos[1], 80.0f), hi[2]};
    for (const MonsterActor& o : actors) {
        if (o.slot == a.slot || o.state != 2) continue;
        const float s = fmul(fmul(0.1f, o.mon_radius), 2.0f);
        for (CCPoly q : shape_.polys) {   // CFrame::PickUpNearPoly: vertices through the frame's matrix (VU0), normals rebuilt from the new vertices (trance_normal)
            for (int v = 0; v < 3; ++v)
                for (int i = 0; i < 3; ++i) q.v[v][i] = gm::vadd(gm::vmul(q.v[v][i], s), o.pos[i]);
            if (!overlaps(q, lo2, hi2)) continue;
            const vu0::V3 e1 = vu0::sub(q.v[1], q.v[0]), e2 = vu0::sub(q.v[2], q.v[0]);
            const vu0::V3 nn = vu0::outer(e1.data(), e2.data());
            q.n[0] = nn[0]; q.n[1] = nn[1]; q.n[2] = nn[2];
            polys_.push_back(q);
        }
    }
}

// CMonstorUnit::PalletStep (the part that matters for the simulation): the fade.
static void pallet_step(MonsterActor& a) {
    if (a.alpha_wait == 0) {
        a.alpha = fsub(a.alpha, a.alpha_rate);
        if (a.alpha <= 0.0f) a.alpha = 0.0f;
        if (128.0f <= a.alpha) a.alpha = 128.0f;
    } else {
        --a.alpha_wait;
    }
}

void MonsterWorld::move_check2(MonsterActor& a) {
    // Stops a monster that is walking into the player.
    float to[3] = {fadd(a.pos[0], fmul(a.move_dir[0], a.move_speed)), fadd(a.pos[1], fmul(a.move_dir[1], a.move_speed)), fadd(a.pos[2], fmul(a.move_dir[2], a.move_speed))};
    float step[3] = {fsub(to[0], a.pos[0]), 0.0f, fsub(to[2], a.pos[2])};
    vu_normalize(step, step);
    const float pflat[3] = {player.pos[0], 1.0f, player.pos[2]}, tflat[3] = {to[0], 1.0f, to[2]};   // the game sets y of both to 1.0 before measuring
    const float d = vu_dist(pflat, tflat);
    if (trace) log.push_back("move_check2 slot " + std::to_string(a.slot) + " d " + std::to_string(d) + " lim " + std::to_string(fadd(a.body_radius, 6.0f)) + " to.y " + std::to_string(to[1]) + " py " + std::to_string(player.pos[1]));
    if (d <= fadd(a.body_radius, 6.0f) && to[1] < fadd(player.pos[1], 18.0f)) {
        float toward[3] = {fsub(player.pos[0], a.pos[0]), 0.0f, fsub(player.pos[2], a.pos[2])};
        vu_normalize(toward, toward);
        const float dot = fadd(fadd(fmul(step[0], toward[0]), fmul(step[1], toward[1])), fmul(step[2], toward[2]));
        if (0.0f < dot) {
            a.move_dir[0] = a.move_dir[1] = a.move_dir[2] = 0.0f;
            a.move_speed = 0.0f;
        }
    }
}

void MonsterWorld::move_check_player(const float pos[3], float vel[3], int character, bool lock_on) {
    static constexpr float kHeight[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};
    const float end[3] = {fadd(pos[0], vel[0]), fadd(pos[1], vel[1]), fadd(pos[2], vel[2])};
    float dir[3];
    vu_normalize(vel, dir);
    const float top_p = fadd(end[1], kHeight[std::clamp(character, 0, 5)]);
    float bot_p = end[1];
    const float flat_end[3] = {end[0], 1.0f, end[2]};
    auto stop_if_toward = [&](const float sx, const float sz) {
        float to[3] = {fsub(sx, pos[0]), 0.0f, fsub(sz, pos[2])};
        vu_normalize(to, to);
        const float dot = fadd(fadd(fmul(dir[0], to[0]), fmul(dir[1], to[1])), fmul(dir[2], to[2]));
        if (0.3333333432674408f < dot) {
            vel[0] = 0.0f;
            vel[1] = fsub(vel[1], 2.0f);
            vel[2] = 0.0f;
            return true;
        }
        return false;
    };
    for (MonsterActor& m : actors) {
        if (m.state != 2 || m.active == 0) continue;
        int count = 0;
        for (const MovCol& d : m.mov_cols) count += d.active ? 1 : 0;
        if (count == 0) {   // one body: a cylinder of the wall radius
            float my = m.pos[1];
            if (lock_on) { my = 1.0f; bot_p = 1.0f; }
            const float flat_m[3] = {m.pos[0], 1.0f, m.pos[2]};
            const float d = vu_dist(flat_m, flat_end);
            if (!(d < fadd(m.body_radius, 6.0f))) continue;
            bool clear = true;
            const float top_m = fadd(my, fmul(m.body_radius, 2.0f));
            if (top_p <= top_m && my < top_p) clear = false;
            if (bot_p <= top_m && my < bot_p) clear = false;
            if (top_m <= top_p && bot_p < my) clear = false;
            if (top_p <= top_m && my < bot_p) clear = false;
            if (clear) continue;
            if (stop_if_toward(m.pos[0], m.pos[2])) return;
        } else {
            for (const MovCol& s : m.mov_cols) {
                if (!s.active) continue;
                float sy = s.pos[1];
                const float flat_s[3] = {s.pos[0], 1.0f, s.pos[2]};
                const float d = vu_dist(flat_s, flat_end);
                if (!(d <= fadd(s.radius, 6.0f))) continue;
                if (lock_on) { bot_p = 1.0f; sy = 1.0f; }
                const float lo = fsub(sy, s.radius), hi = fadd(sy, s.radius);
                bool clear = true;
                if (top_p < lo && hi < top_p) clear = false;
                if (bot_p < lo && hi < bot_p) clear = false;
                if (lo < top_p && bot_p < hi) clear = false;
                if (top_p < lo && hi < bot_p) clear = false;
                if (clear) continue;
                if (stop_if_toward(s.pos[0], s.pos[2])) return;
            }
        }
    }
}

void MonsterWorld::move_check_monster(MonsterActor& a) {
    // Stops a monster that is walking into another monster.
    float to[3] = {fadd(a.pos[0], fmul(a.move_dir[0], a.move_speed)), fadd(a.pos[1], fmul(a.move_dir[1], a.move_speed)), fadd(a.pos[2], fmul(a.move_dir[2], a.move_speed))};
    float step[3] = {fsub(to[0], a.pos[0]), 0.0f, fsub(to[2], a.pos[2])};
    vu_normalize(step, step);
    for (MonsterActor& o : actors) {
        if (o.state != 2 || o.slot == a.slot) continue;
        const float oflat[3] = {o.pos[0], 1.0f, o.pos[2]}, tflat[3] = {to[0], 1.0f, to[2]};
        const float d = vu_dist(oflat, tflat);
        if (d <= fadd(o.mon_radius, a.mon_radius) && to[1] < fadd(o.pos[1], fmul(o.mon_radius, 2.0f))) {
            float toward[3] = {fsub(o.pos[0], a.pos[0]), 0.0f, fsub(o.pos[2], a.pos[2])};
            vu_normalize(toward, toward);
            const float dot = fadd(fadd(fmul(step[0], toward[0]), fmul(step[1], toward[1])), fmul(step[2], toward[2]));
            if (0.0f < dot) {
                a.move_dir[0] = a.move_dir[1] = a.move_dir[2] = 0.0f;
                a.move_speed = 0.0f;
                return;
            }
        }
    }
}

void MonsterWorld::step_actor(MonsterActor& a) {
    cur_ = a.slot;
    if (double_weapon_exp && a.active != 0) a.bin2 = 0xb4;   // the back floor's monsters are kept powered up (CMonstorUnit::Step, unit +0x44)
    if (a.motion_restore) {   // the hit pause ended: the monster's last motion comes back
        a.last_motion_flags &= ~4;
        a.request.id = a.last_motion_id;
        a.request.flags = a.last_motion_flags == 2 ? 2u : 0u;
        a.request.speed = 0.0f <= a.last_motion_speed ? a.last_motion_speed : -1.0f;
        a.motion_restore = false;
    }
    gather_polys(a, true);
    int n = int(polys_.size());
    const int hit = check_dmg(a);
    if (hit == 2) {
        if (a.vm.has_function(120)) a.vm.run(120);
        a.script_active = true;
    } else if (hit == 1) {
        if (a.vm.has_function(110)) a.vm.run(110);
        a.script_active = true;
    }
    if (a.hit_stop >= 1) {
        fire_shots(a);
        pallet_step(a);
        if (a.muteki > 0) --a.muteki;
        return;
    }
    if (trace && a.slot == 0 && !a.vm.trace) { a.vm.trace = true; a.vm.log = [this](const std::string &t) { if (log.size() < 300000) log.push_back("vm " + t); }; }
    if (!a.script_active) {
        if (a.active == 1) {
            if (a.vm.has_function(50)) a.vm.run(50);
            a.active = -1;
        } else if (a.vm.has_function(100)) {
            a.vm.run(100);
        }
        a.script_active = true;
    } else {
        a.vm.resume();
        if (!a.vm.running()) a.script_active = false;
    }
    if (0.0f < a.push_speed) {
        std::copy(a.push_dir, a.push_dir + 4, a.move_dir);
        a.move_speed = a.push_speed;
        a.push_speed = fsub(a.push_speed, a.push_decay);
        if (a.push_speed <= 0.0f) {
            a.push_speed = 0.0f;
            a.move_speed = 0.0f;
        }
    }
    if (a.col_off < 1) move_check2(a);
    if (a.col_off < 1) move_check_monster(a);
    n = terrain_count_;   // the monsters' polygons are dropped again before the movement tests (0x1e420 = count before PickUpNearPoly)

    float pos[3] = {a.pos[0], a.pos[1], a.pos[2]};
    if (a.move_speed != 0.0f || (a.fall != 0 && a.move_speed == 0.0f)) {
        float to[3] = {fadd(pos[0], fmul(a.move_dir[0], 10.0f)), fadd(pos[1], fmul(a.move_dir[1], 10.0f)), fadd(pos[2], fmul(a.move_dir[2], 10.0f))};
        pos[1] = fadd(pos[1], 5.0f);
        to[1] = fadd(to[1], 5.0f);
        float hit[3];
        const int h = check_hit(polys_.data(), n, pos, to, hit, false, 0);
        if (trace) log.push_back("move slot " + std::to_string(a.slot) + " h " + std::to_string(h) + " polys " + std::to_string(n) + " from " + std::to_string(pos[0]) + "," + std::to_string(pos[1]) + "," + std::to_string(pos[2]) + " hit " + std::to_string(hit[0]) + "," + std::to_string(hit[1]) + "," + std::to_string(hit[2]));
        if (trace && h >= 0) { const CCPoly &q = polys_[size_t(h)]; log.push_back("blocker type " + std::to_string(q.type()) + " v0 " + std::to_string(q.v[0][0]) + "," + std::to_string(q.v[0][1]) + "," + std::to_string(q.v[0][2]) + " v1 " + std::to_string(q.v[1][0]) + "," + std::to_string(q.v[1][1]) + "," + std::to_string(q.v[1][2]) + " v2 " + std::to_string(q.v[2][0]) + "," + std::to_string(q.v[2][1]) + "," + std::to_string(q.v[2][2]) + " n " + std::to_string(q.n[0]) + "," + std::to_string(q.n[1]) + "," + std::to_string(q.n[2])); }
        if (h < 0 || a.col_off > 0 || (boss_free_flight && a.slot == 0)) {
            pos[1] = fsub(pos[1], 5.0f);
            float vel[3];
            vel[0] = fmul(a.move_dir[0], a.move_speed);
            vel[1] = fmul(a.move_dir[1], a.move_speed);
            vel[2] = fmul(a.move_dir[2], a.move_speed);
            if (a.fall != 0 && !(a.height <= kTiny)) vel[1] = fsub(vel[1], kGravity);
            const float np[3] = {fadd(pos[0], vel[0]), fadd(pos[1], vel[1]), fadd(pos[2], vel[2])};
            std::copy(np, np + 3, a.pos);
            if (a.fall != 0 && kTiny < a.height) {
                a.move_speed = vu_dist(pos, np);
                const float d[3] = {fsub(np[0], pos[0]), fsub(np[1], pos[1]), fsub(np[2], pos[2])};
                vu_normalize(d, a.move_dir);
            }
        } else {
            pos[1] = fsub(pos[1], 5.0f);
            a.move_speed = 0.0f;
            std::copy(pos, pos + 3, a.pos);
        }
        float p[3] = {a.pos[0], fadd(a.pos[1], 5.0f), a.pos[2]};
        float out[3];
        if (a.col_off < 1 && !(boss_free_flight && a.slot == 0) && check_width(a.body_radius, polys_.data(), n, p, out, 0) != 0) {
            out[1] = fsub(out[1], 5.0f);
            std::copy(out, out + 3, a.pos);
        }
    }
    if (boss_hover_cap > 0.0f && a.slot == 0 && a.pos[1] > boss_hover_cap) a.pos[1] = boss_hover_cap;
    a.height = 0.0f;
    {
        float p[3] = {a.pos[0], fadd(a.pos[1], 10.0f), a.pos[2]};
        float hit[3];
        if (check_hit_vertical(-130.0f, polys_.data(), n, p, hit, 0) >= 0) {
            p[1] = fsub(p[1], 10.0f);
            if (p[1] < hit[1]) {
                p[1] = hit[1];
                if (a.fall != 0) {
                    a.move_dir[1] = fmul(a.move_dir[1], -1.0f);
                    if (0.0f < a.move_dir[1]) {
                        a.move_speed = fmul(fmul(a.move_speed, fsub(2.0f, a.move_dir[1])), 0.5f);
                        if (a.move_speed < kGravity) a.move_speed = 0.0f;
                    }
                }
            }
            a.height = fsub(p[1], hit[1]);
            a.floor_y = hit[1];
            if (a.fall != 0) {
                std::copy(p, p + 3, a.pos);
                if (a.gravity_mode == 0) std::copy(hit, hit + 3, a.pos);
            }
        }
    }
    if (a.rot_speed != 0.0f) {
        const float d[3] = {fsub(a.face_target[0], a.pos[0]), fsub(a.face_target[1], a.pos[1]), fsub(a.face_target[2], a.pos[2])};
        const float ang = ps2_atan2f(d[0], d[2]);
        a.rot[1] = angle_interpolate(a.rot[1], ang, a.rot_speed, rotation_mode);
        if (angle_cmp(a.rot[1], ang, kRotDone) == 0) a.rot_speed = 0.0f;
    }
    fire_shots(a);
    pallet_step(a);
    if (a.muteki > 0) --a.muteki;
    if (a.col_off > 0) --a.col_off;
    if (trace && a.slot == 0) {
        static int dbg_tick = 0;
        if (++dbg_tick % 150 == 0) {
            std::string t = "bodycols pos " + std::to_string(a.pos[0]) + "," + std::to_string(a.pos[1]) + "," + std::to_string(a.pos[2]) + ":";
            for (const BodyCol& b : a.body_cols) t += " [" + b.bone + (b.active ? " on " : " off ") + std::to_string(b.pos[0]) + "," + std::to_string(b.pos[1]) + "," + std::to_string(b.pos[2]) + " r" + std::to_string(b.radius) + "]";
            log.push_back(t);
        }
    }
    if (a.state == -1) drop_loot(a);
}

void MonsterWorld::step_motion(MonsterActor& a) {
    PoseSample sample;
    const float t0 = step_motion_state(a.table, a.request, a.motion, a.motion_phase, &sample);
    if (trace && a.shot[0].state != 0) log.push_back("step_motion slot " + std::to_string(a.slot) + " t0 " + std::to_string(t0) + " req " + std::to_string(a.request.id) + " tablesize " + std::to_string(a.table.size()));
    if (t0 < 0.0f) return;
    update_cols(a, sample);   // the monster is drawn: its spheres get their world positions for the next frame's hit tests
}

void MonsterWorld::step_ai() {
    check_view_level();
    if (hold != 0) {
        paused = true;
        return;
    }
    if (paused) {
        // the frame after a hold: put the last requested motion back (the draw froze it)
        for (MonsterActor& a : actors) {
            if (a.state != 2) continue;
            a.last_motion_flags &= ~4;
            a.request.id = a.last_motion_id;
            a.request.flags = a.last_motion_flags == 2 ? 2u : 0u;
            a.request.speed = 0.0f <= a.last_motion_speed ? a.last_motion_speed : -1.0f;
        }
        paused = false;
    }
    for (MonsterActor& a : actors) {
        if (a.state != 2 || a.active == 0) continue;
        RemotePlayer* target = nullptr;   // co-op: a monster goes for the nearest player
        float best = vu_dist(player.pos, a.pos);
        for (auto& r : remotes) {
            if (!r) continue;
            const float d = vu_dist(r->info.pos, a.pos);
            if (d < best) { best = d; target = r.get(); }
        }
        if (target) std::swap(player, target->info);
        step_actor(a);
        if (target) std::swap(player, target->info);
    }
}

void MonsterWorld::step_motions() {
    for (MonsterActor& a : actors) {
        if (a.state != 2 || a.active == 0) continue;
        if (paused || a.hit_stop > 0) {   // DrawMonstor freezes the motion while the unit is held or the monster is in hit-stop
            a.request.flags = 1;
            a.request.speed = -1.0f;
        }
        step_motion(a);
        if (!paused) sound_check(a);
    }
}

// SoundCheck: for each registered frame sound that is not cooling down, the motion time inside [start, end) plays it (and starts a 10 tick pause).
void MonsterWorld::sound_check(MonsterActor& a) {
    const float t = a.motion.time;
    for (auto& s : a.snd_frames) {
        if (s.cool < 1) {
            if (s.id != -1 && s.start <= t && t < s.end) {
                sounds.push_back({s.id, {a.pos[0], a.pos[1], a.pos[2]}, a.kind_boss, false});
                s.cool = 10;
            }
        } else {
            --s.cool;
        }
    }
}

bool MonsterWorld::ext(int id, const ScriptArgs& a) {
    MonsterActor& m = actors[size_t(cur_)];
    const int n = a.n;
    if (trace_ext) {
        std::string t = std::to_string(cur_) + " " + std::to_string(id) + " " + std::to_string(n);
        for (int i = 0; i < n && i < 9; ++i) {
            char buf[64];
            std::snprintf(buf, sizeof buf, " %d:%d", a.type(i), a.type(i) == 3 ? 0 : a.get_int(i));
            if (a.type(i) == 1) std::snprintf(buf, sizeof buf, " 1:%08x", [&] { const float f = a.get_float(i); uint32_t u; std::memcpy(&u, &f, 4); return u; }());
            t += buf;
        }
        ext_log.push_back(t);
    }
    if (trace) {
        std::string t = "slot " + std::to_string(cur_) + " ext " + std::to_string(id) + " (";
        for (int i = 0; i < n; ++i) {
            char buf[64];
            if (a.type(i) == 3) std::snprintf(buf, sizeof buf, " ptr");
            else if (a.type(i) == 1) std::snprintf(buf, sizeof buf, " %g", double(a.get_float(i)));
            else std::snprintf(buf, sizeof buf, " %d", a.get_int(i));
            t += buf;
        }
        log.push_back(t + " )");
    }
    auto ignore = [&](const char* name) {
        if (trace) log.push_back(std::string(name) + " (ignored)");
        return true;
    };
    switch (id) {
        case 10: {   // _GET_DISTANCE
            float target[3];
            int out;
            if (n == 1) {
                std::copy(player.pos, player.pos + 3, target);
                out = 0;
            } else {
                target[0] = a.get_float(0); target[1] = a.get_float(1); target[2] = a.get_float(2);
                out = 3;
            }
            a.set_float(out, vu_dist(m.pos, target));
            return true;
        }
        case 11: {   // _GET_POSITION (-1 = this monster, -2 = the player)
            const int sel = ftoi(a.get_float(0));
            const float* p = sel == -2 ? player.pos : m.pos;
            a.set_float(1, p[0]); a.set_float(2, p[1]); a.set_float(3, p[2]);
            return true;
        }
        case 12: {   // _SET_ROTATION(x, y, z, speed)
            m.face_target[0] = a.get_float(0); m.face_target[1] = a.get_float(1); m.face_target[2] = a.get_float(2);
            m.rot_speed = a.get_float(3);
            if (m.rot_speed < 0.0f) {
                m.rot[1] = ps2_atan2f(fsub(m.face_target[0], m.pos[0]), fsub(m.face_target[2], m.pos[2]));
                m.rot_speed = 0.0f;
            }
            return true;
        }
        case 13: a.set_int(0, m.rot_speed == 0.0f ? 1 : 0); return true;   // _CHK_ROTATION
        case 14: {   // _CHK_MOVE(out [, distance])
            a.set_int(0, m.move_speed == 0.0f ? 1 : 0);
            if (n == 2) a.set_float(1, vu_dist(m.move_dir, m.pos));
            return true;
        }
        case 15: {   // _CHK_USER_INNER_PRODUCT(angle [, maxdist], out)
            const float tol = fmul(0.011111111380159855f, itof(a.get_int(0)));
            float maxd = 100000.0f;
            int out = 1;
            if (n == 3) { maxd = a.get_float(1); out = 2; }
            float fwd[3];
            facing(m.rot[1], fwd);
            float away[3] = {fsub(m.pos[0], player.pos[0]), 0.0f, fsub(m.pos[2], player.pos[2])};
            vu_normalize(away, away);
            const float dot = fadd(fadd(fmul(fwd[0], away[0]), fmul(fwd[1], away[1])), fmul(fwd[2], away[2]));
            int r = 0;
            if (fsub(1.0f, tol) <= dot && vu_dist(m.pos, player.pos) < maxd) r = 1;
            a.set_int(out, r);
            return true;
        }
        case 30: {   // _GET_VECTOR(x, y, z [, angle], outx, outy, outz): direction to a point
            float v[3] = {fsub(a.get_float(0), m.pos[0]), fsub(a.get_float(1), m.pos[1]), fsub(a.get_float(2), m.pos[2])};
            int out = 3;
            float ang = 0.0f;
            if (n == 7) {
                out = 4;
                ang = a.get_float(3);
                if (180.0f <= ang) ang = fsub(ang, 360.0f);
                ang = fmul(kDegToRad, ang);
                if (kTwoPiF < ang) ang = fsub(ang, kTwoPiF);
                if (ang < -kPiF) ang = fadd(ang, kTwoPiF);
            }
            vu_normalize(v, v);
            if (n == 7) turn_vector(v, ang, v);
            a.set_float(out, v[0]); a.set_float(out + 1, v[1]); a.set_float(out + 2, v[2]);
            return true;
        }
        case 31: {   // _GET_DIRECTION([angle,] outx, outy, outz): the way the monster faces
            float yaw = m.rot[1];
            int out = 0;
            if (n == 4) {
                yaw = fadd(yaw, fmul(kDegToRad, a.get_float(0)));
                if (kPiF < yaw) yaw = fsub(yaw, kTwoPiF);
                if (yaw < -kPiF) yaw = fadd(yaw, kTwoPiF);
                out = 1;
            }
            float v[3];
            facing(yaw, v);
            a.set_float(out, v[0]); a.set_float(out + 1, v[1]); a.set_float(out + 2, v[2]);
            return true;
        }
        case 32: {   // _SET_MOVE(x, y, z, speed)
            const float d[3] = {fsub(a.get_float(0), m.pos[0]), fsub(a.get_float(1), m.pos[1]), fsub(a.get_float(2), m.pos[2])};
            vu_normalize(d, m.move_dir);
            if (trace) { char b[256]; std::snprintf(b, sizeof b, "SET_MOVE slot %d vec %a %a %a -> dir %a %a %a", cur_, double(d[0]), double(d[1]), double(d[2]), double(m.move_dir[0]), double(m.move_dir[1]), double(m.move_dir[2])); log.push_back(b); }
            float sp = a.get_float(3);
            if (0 < m.status_slow) sp = fmul(sp, 0.5f);
            m.move_speed = sp;
            return true;
        }
        case 33: {   // _CHK_MOVE_INFO(x, y, z, out): 1 when the way to the point is free
            float from[3] = {m.pos[0], fadd(m.pos[1], 5.0f), m.pos[2]};
            float to[3] = {a.get_float(0), fadd(a.get_float(1), 5.0f), a.get_float(2)};
            float hit[3];
            gather_polys(m, true);
            const int ch = check_hit(polys_.data(), int(polys_.size()), from, to, hit, false, 0);
            if (trace) log.push_back("chk_move_info slot " + std::to_string(m.slot) + " polys " + std::to_string(polys_.size()) + " from " + std::to_string(from[0]) + "," + std::to_string(from[1]) + "," + std::to_string(from[2]) + " to " + std::to_string(to[0]) + "," + std::to_string(to[1]) + "," + std::to_string(to[2]) + " hit " + std::to_string(ch));
            a.set_int(3, (ch < 0 || (boss_free_flight && m.slot == 0)) ? 1 : 0);
            return true;
        }
        case 34: m.move_speed = 0.0f; return true;      // _SET_MOVE_CANSEL
        case 35: m.rot_speed = 0.0f; return true;       // _SET_ROT_CANSEL
        case 36: m.pos[0] = a.get_float(0); m.pos[1] = a.get_float(1); m.pos[2] = a.get_float(2); return true;   // _SET_POSITION
        case 100: m.fall = a.get_int(0); return true;   // _STATUS_SET_FALL
        case 101: {   // _STATUS_SET_MUTEKI
            int v = a.get_int(0);
            m.muteki = v;
            return true;
        }
        case 102:   // _STATUS_SET_ALPHA(rate [, rate as int]): how fast the monster fades (the game's second form overwrites the rate with the integer argument)
            m.alpha_rate = a.get_float(0);
            if (n == 2) m.alpha_rate = float(a.get_int(1));
            else m.alpha_wait = 0;
            return true;
        case 103: {   // _STATUS_CHK_ALPHA([mode,] out)
            int mode = 0, out = 0;
            if (n == 2) { mode = a.get_int(0); out = 1; }
            const bool ok = mode == 0 ? m.alpha <= 0.0f : 128.0f <= m.alpha;
            a.set_int(out, ok ? 1 : 0);
            return true;
        }
        case 104: m.state = -1; --alive; return true;   // _STATUS_SET_DEAD
        case 105: return ignore("STATUS_SET_PALLET");
        case 106: m.clip_level = a.get_float(0); return true;   // _STATUS_SET_CLIPLEVEL
        case 108: m.col_off = a.get_int(0); return true;        // _STATUS_SET_COL_OFF
        case 109: a.set_float(0, fmul(fdiv(itof(m.hp), itof(m.hp_max)), 100.0f)); return true;   // _STATUS_GET_LIFE_RATE
        case 110: {   // _STATUS_GET_USER_VECTOR([normalize,] angle?, outx, outy, outz): approximated from the player's facing
            return ignore("STATUS_GET_USER_VECTOR");
        }
        case 112: a.set_float(0, m.height); return true;        // _STATUS_GET_HEIGHT
        case 113: a.set_int(0, m.hit_vol); return true;         // _STATUS_GET_HITDMG_VOL
        case 114: a.set_int(0, m.request.id); return true;      // _STATUS_GET_MOTION_ID
        case 115: a.set_int(0, m.dmg_id); return true;          // _STATUS_GET_DMG_ID
        case 116: m.lockon_dist = a.get_float(0); return true;  // _STATUS_SET_LOCKON_DIST
        case 130: {   // _SET_BODY_COL(bone, radius [, window start, window end])
            const std::string bone = a.get_string(0);
            for (int i = 0; i < 16; ++i) {
                BodyCol& b = m.body_cols[size_t(i)];
                if (b.active) continue;
                b = BodyCol{};
                b.active = true;
                b.bone = bone;
                b.radius = a.get_float(1);
                if (n == 4) { b.win_start = a.get_float(2); b.win_end = a.get_float(3); }
                bak_body_col = i;
                break;
            }
            return true;
        }
        case 136: {   // _SET_MOV_COL(bone, radius): a sphere the player bumps into
            const std::string bone = a.get_string(0);
            const MonsterBase& base = unit_->bases[size_t(m.base)];
            bool found = false;
            if (base.model)
                for (const Joint& j : base.model->joints)
                    if (j.name == bone) found = true;
            for (int i = 0; i < 12 && found; ++i) {
                MovCol& d = m.mov_cols[size_t(i)];
                if (d.active) continue;
                d = MovCol{};
                d.active = true;
                d.bone = bone;
                d.radius = a.get_float(1);
                break;
            }
            return true;
        }
        case 134: {   // _SET_BODY_COL_PARA(index, value): element percents (0..4) and per-character percents (10..15) of the last body sphere
            const int idx = a.get_int(0), v = a.get_int(1);
            if (bak_body_col != -1) {
                BodyCol& b = m.body_cols[size_t(bak_body_col)];
                if (idx >= 0 && idx < 5) b.element_pct[size_t(idx)] = v;
                else if (idx >= 10 && idx < 16) b.attacker_pct[size_t(idx - 10)] = v;
            }
            return true;
        }
        case 131: {   // _SET_DMG_COL(bone, radius, start, end)
            const std::string bone = a.get_string(0);
            m.cur_dmg_col = -1;
            for (int i = 0; i < 16; ++i) {
                DmgCol& d = m.dmg_cols[size_t(i)];
                if (d.active) continue;
                d = DmgCol{};
                d.active = true;
                d.bone = bone;
                d.radius = a.get_float(1);
                d.win_start = a.get_float(2);
                d.win_end = a.get_float(3);
                m.cur_dmg_col = i;
                break;
            }
            return true;
        }
        case 132: {   // _SET_DMG_PARA(damage, flags, type [, angle])
            if (m.cur_dmg_col != -1) {
                DmgCol& d = m.dmg_cols[size_t(m.cur_dmg_col)];
                d.damage = a.get_int(0);
                d.flags = a.get_int(1);
                d.type = a.get_int(2);
                d.angle = 0.0f;
                if (d.type == 3 && n == 4) d.angle = a.get_float(3);
            }
            return true;
        }
        case 244: {   // _SET_GUARD_FRAME(start, end)
            const int s = ftoi(a.get_float(0)), e = ftoi(a.get_float(1));
            for (int g = 0; g < 3; ++g)
                if (m.guard_on[size_t(g)] == 0) {
                    m.guard_on[size_t(g)] = 1;
                    m.guard_start[size_t(g)] = itof(s);
                    m.guard_end[size_t(g)] = itof(e);
                    break;
                }
            return true;
        }
        case 133: case 229: {   // _SET_SHOT(frame, x, y, z [, damage]) / _SET_SHOT2: aim a shot effect from a frame of the model at a point
            const int k = id == 133 ? 0 : 1;
            if (m.shot_slot[size_t(k)] == -1) return true;
            ShotRegistration& r = m.shot[size_t(k)];
            if (trace) log.push_back("SET_SHOT slot " + std::to_string(m.slot) + " state " + std::to_string(r.state));
            r.target[0] = a.get_float(1); r.target[1] = a.get_float(2); r.target[2] = a.get_float(3); r.target[3] = 1.0f;
            if (r.state == 0) {
                r.bone = a.get_string(0);
                const MonsterBase& base = unit_->bases[size_t(m.base)];
                r.joint = -1;
                if (base.model)
                    for (size_t j = 0; j < base.model->joints.size(); ++j)
                        if (base.model->joints[j].name == r.bone) r.joint = int(j);
                if (trace) log.push_back("SET_SHOT joint " + std::to_string(r.joint) + " bone " + r.bone);
                if (r.joint < 0) return true;
                r.state = 1;
            }
            r.damage = n == 5 ? a.get_int(4) : -1;
            return true;
        }
        case 118: {   // _STATUS_SET_LOCKON_TRG(frame [, scale x, scale y]): the lock-on cursor sits on this frame of the model
            const MonsterBase& base = unit_->bases[size_t(m.base)];
            const std::string bone = a.get_string(0);
            m.lock_joint = -1;
            if (base.model)
                for (size_t j = 0; j < base.model->joints.size(); ++j)
                    if (base.model->joints[j].name == bone) m.lock_joint = int(j);
            if (m.lock_joint >= 0) m.lock_bone = bone;
            return true;
        }
        case 117:
        case 140: {   // _SET_SND_FRM(start, end, id): the first free slot takes it
            for (auto& s : m.snd_frames) {
                if (s.id != -1) continue;
                if (s.cool == 0) { s.start = a.get_float(0); s.end = a.get_float(1); s.id = a.get_int(2); }
                break;
            }
            return true;
        }
        case 141: sounds.push_back({a.get_int(0), {m.pos[0], m.pos[1], m.pos[2]}, m.kind_boss, false}); return true;   // _SET_SND_NOW
        case 142: sounds.push_back({a.get_int(0), {m.pos[0], m.pos[1], m.pos[2]}, m.kind_boss, true}); return true;    // _STOP_SND_NOW
        case 230: case 231: case 232:
            return ignore("registration");
        case 180: a.set_int(1, scaled(a.get_int(0), rng_->next())); return true;   // _GET_RAND(max, out)
        case 181: {   // _GET_RANDF(max, out): the game truncates the product to an integer first
            const float mx = a.get_float(0);
            a.set_float(1, itof(ftoi(fdiv(fmul(mx, itof(rng_->next())), kRandRange))));
            return true;
        }
        case 182: a.set_float(1, ps2_sinf(fmul(kDegToRad, a.get_float(0)))); return true;   // _SIN_DEG(angle, out)
        case 183: a.set_float(1, ps2_cosf(fmul(kDegToRad, a.get_float(0)))); return true;   // _COS_DEG
        case 200: {   // _SET_MOTION(id [, speed [, flags]])
            const int mid = a.get_int(0);
            m.hit_vol = -1;
            float sp;
            uint32_t flags = 0;
            if (n == 1) {
                sp = size_t(mid) < m.table.size() ? m.table[size_t(mid)].speed : 0.0f;
            } else {
                sp = a.get_float(1);
                if (n == 3) flags = uint32_t(a.get_int(2));
            }
            if (0 < m.status_slow) sp = fmul(sp, 0.5f);
            m.request = MotionRequest{sp, flags, mid};
            m.last_motion_id = mid;
            m.last_motion_flags = int(flags);
            m.last_motion_speed = sp;
            return true;
        }
        case 201: {   // _CHK_MOTION_FRM(out): on the last frame of the motion
            int r = 0;
            if (size_t(m.request.id) < m.table.size()) {
                const float end = float(m.table[size_t(m.request.id)].end);
                if (fsub(end, 1.0f) <= m.motion.time && m.motion.time < end) r = 1;
            }
            a.set_int(0, r);
            if (n == 2) a.set_float(1, m.motion.time);   // the second form also reports the current frame
            return true;
        }
        case 202: if (trace) log.push_back("frm " + std::to_string(m.motion.time) + " id " + std::to_string(m.request.id) + " start " + std::to_string(size_t(m.request.id) < m.table.size() ? m.table[size_t(m.request.id)].start : -1) + " end " + std::to_string(size_t(m.request.id) < m.table.size() ? m.table[size_t(m.request.id)].end : -1) + " speed " + std::to_string(size_t(m.request.id) < m.table.size() ? m.table[size_t(m.request.id)].speed : -1.0f)); a.set_float(0, m.motion.time); return true;   // _GET_MOTION_FRM
        case 203: m.motion.time = a.get_float(0); return true;  // _SET_MOTION_FRM
        case 204: case 205: return ignore("status/texture");
        case 206: m.mon_radius = a.get_float(0); return true;   // _SET_COLLISION_WIDTH
        case 207: {   // _GET_NEAR_MONSTER(x, y, z, id)
            float best = 240.0f;
            int pick = -1;
            for (const MonsterActor& o : actors) {
                if (o.slot == m.slot || o.state != 2) continue;
                const float d = vu_dist(m.pos, o.pos);
                if (d < best) { best = d; pick = o.slot; }
            }
            if (pick >= 0) {
                a.set_float(0, actors[size_t(pick)].pos[0]); a.set_float(1, actors[size_t(pick)].pos[1]); a.set_float(2, actors[size_t(pick)].pos[2]);
            }
            a.set_int(3, pick);
            return true;
        }
        case 208: a.set_int(0, m.bin2); return true;            // _GET_STATUS_BIN2
        case 209: m.bin2 = a.get_int(0); return true;           // _SET_BIN2
        case 210: a.set_int(0, player.character); return true;  // _GET_CHR_ID
        case 211: a.set_int(0, 0); return true;                 // _GET_COL_HIT_ID (no collisions registered yet)
        case 212: a.set_int(0, m.slot); return true;            // _GET_SCRIPT_ID
        case 213: {   // _GET_MONSTOR_POS(id, x, y, z)
            const int k = a.get_int(0);
            if (k >= 0 && k < MonsterUnit::kSlots) {
                a.set_float(1, actors[size_t(k)].pos[0]); a.set_float(2, actors[size_t(k)].pos[1]); a.set_float(3, actors[size_t(k)].pos[2]);
            }
            return true;
        }
        case 214: {   // _GET_MONSTOR_FRM(id, out)
            const int k = a.get_int(0);
            if (k >= 0 && k < MonsterUnit::kSlots) a.set_float(1, actors[size_t(k)].motion.time);
            return true;
        }
        case 215: {   // _SET_MONSTOR_POS(id, x, y, z)
            const int k = a.get_int(0);
            if (k >= 0 && k < MonsterUnit::kSlots) { actors[size_t(k)].pos[0] = a.get_float(1); actors[size_t(k)].pos[1] = a.get_float(2); actors[size_t(k)].pos[2] = a.get_float(3); }
            return true;
        }
        case 216: {   // _SET_MONSTOR_MOVE(id, x, y, z, speed)
            const int k = a.get_int(0);
            if (k >= 0 && k < MonsterUnit::kSlots) {
                MonsterActor& o = actors[size_t(k)];
                const float d[3] = {fsub(a.get_float(1), o.pos[0]), fsub(a.get_float(2), o.pos[1]), fsub(a.get_float(3), o.pos[2])};
                vu_normalize(d, o.move_dir);
                o.move_speed = a.get_float(4);
            }
            return true;
        }
        case 217: {   // _SET_MONSTOR_LINK_MOVE(to, from)
            const int k = a.get_int(0), j = a.get_int(1);
            if (k >= 0 && k < MonsterUnit::kSlots && j >= 0 && j < MonsterUnit::kSlots) {
                std::copy(actors[size_t(j)].move_dir, actors[size_t(j)].move_dir + 4, actors[size_t(k)].move_dir);
                actors[size_t(k)].move_speed = actors[size_t(j)].move_speed;
            }
            return true;
        }
        case 218: {   // _SET_MONSTOR_MOVE_CANSEL(id)
            const int k = a.get_int(0);
            if (k >= 0 && k < MonsterUnit::kSlots) actors[size_t(k)].move_speed = 0.0f;
            return true;
        }
        case 219: {   // _SET_MONSTOR_MOTION(id, motion [, speed [, flags]])
            const int k = a.get_int(0), mid = a.get_int(1);
            if (k < 0 || k >= MonsterUnit::kSlots) return true;
            MonsterActor& o = actors[size_t(k)];
            o.hit_vol = -1;
            float sp = -1.0f;
            uint32_t flags = 0;
            if (n >= 3) sp = a.get_float(2);
            if (n == 4) flags = uint32_t(a.get_int(3));
            o.request = MotionRequest{sp, flags, mid};
            return true;
        }
        case 220: {   // _SET_GLOBAL_INT
            const int k = a.get_int(0);
            if (k >= 0 && size_t(k) < globals.size()) globals[size_t(k)] = a.get_int(1);
            return true;
        }
        case 221: {   // _GET_GLOBAL_INT
            const int k = a.get_int(0);
            a.set_int(1, k >= 0 && size_t(k) < globals.size() ? globals[size_t(k)] : 0);
            return true;
        }
        case 222: {   // _GET_OBJ_POS(name, x, y, z): the position of a bone; approximated by the monster's own position
            a.set_float(1, m.pos[0]); a.set_float(2, m.pos[1]); a.set_float(3, m.pos[2]);
            return true;
        }
        case 249: {   // _GET_USER_STATUS(flag, out): the strength value of the character's active status ailment (0 while healthy or when flag is 0)
            const int ch = std::clamp(status.character(), 0, 5);
            int v = 0;
            if (status.get<uint32_t>(0x42c8 + size_t(ch) * 4) != 0 && a.get_int(0) != 0) v = status.get<int16_t>(0x42e0 + size_t(ch) * 2);
            a.set_int(1, v);
            return true;
        }
        case 250: case 251: return true;   // _SET_REFERENCE / _DEL_REFERENCE: only link a frame to another frame's matrix (a model effect, no gameplay)
        case 227: m.lockon_sw = a.get_int(0); return true;      // _SET_LOCKON_SW
        case 228: m.hp = ftoi(fmul(itof(m.hp_max), a.get_float(0))); return true;   // _STATUS_SET_LIFE
        case 242: m.gravity_mode = int16_t(a.get_int(0)); return true;   // _SET_GRAVITY
        case 245: {   // _GUARD_SEARCH(out idle, out distance, out character): has the player attacked lately, and is he pushing toward this monster?
            const float mp[3] = {m.pos[0], m.pos[1], m.pos[2]};
            const float d = vu_dist(player.prev_pos, mp);
            float to[3] = {fsub(mp[0], player.prev_pos[0]), 0.0f, fsub(mp[2], player.prev_pos[2])};
            vu_normalize(to, to);
            const float inner = vu0::inner(player.facing, to);
            if (0.35 <= double(inner)) player.attack_idle = 0xe10;
            if (trace) log.push_back("GUARD_SEARCH slot " + std::to_string(m.slot) + " idle " + std::to_string(player.attack_idle) + " dist " + std::to_string(d) + " inner " + std::to_string(inner));
            a.set_int(0, player.attack_idle);
            a.set_float(1, d);
            a.set_int(2, player.character);
            return true;
        }
        case 107: case 111:   // _STATUS_SET_EVENT / _RUN_SCRIPT: ask the dungeon for an event (a boss's death runs event 510)
            run_script_request = a.get_int(0);
            run_script_slot = m.slot;
            return true;
        case 246: case 252: case 253: case 223: case 225:
            return ignore("misc");
        default:
            if (trace) log.push_back("unhandled ext " + std::to_string(id));
            return true;
    }
}

}  // namespace dccore

namespace dccore {

// The end of CMonstorUnit::Step: a registered shot whose muzzle position is known leaves the monster.
void MonsterWorld::fire_shots(MonsterActor& a) {
    for (size_t k = 0; k < 2; ++k) {
        ShotRegistration& r = a.shot[k];
        if (r.state != 2) continue;
        const int slot = a.shot_slot[k];
        if (slot >= 0) {
            ShotEffect& fx = shots.slot[size_t(slot)];
            const int i = fx.set(r.muzzle, r.target, -1, -1);
            if (i >= 0) {
                fx.inst[size_t(i)].user_id = a.slot;
                if (r.damage >= 0) fx.inst[size_t(i)].damage = r.damage;
            }
        }
        r.state = 0;
        if (trace) log.push_back("fire shot slot " + std::to_string(a.slot) + " fx slot " + std::to_string(slot));
    }
}

// checkCollision: what a shot at `pos` moving by `vel` runs into: the player (2), a monster's hit sphere (3) or a wall (1); `hit` receives the point.
int MonsterWorld::shot_collide(float radius, const float pos[3], const float vel[3], int target, float hit[3]) {
    static constexpr float kHeights[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};
    const float end[3] = {fadd(pos[0], vel[0]), fadd(pos[1], vel[1]), fadd(pos[2], vel[2])};
    if (target != 2) {
        const float p[3] = {player.prev_pos[0], fadd(player.prev_pos[1], kHeights[std::clamp(player.character, 0, 5)]), player.prev_pos[2]};
        if (vu_dist(p, end) <= fadd(radius, 6.0f)) {
            hit[0] = p[0]; hit[1] = p[1]; hit[2] = p[2];
            return 2;
        }
    }
    if (target != 1) {
        for (const MonsterActor& o : actors) {
            if (o.slot < 0 || o.state == -1 || o.active == 0) continue;
            for (const BodyCol& b : o.body_cols) {
                if (!b.active) continue;
                if (vu_dist(b.pos, pos) <= fadd(radius, b.radius)) {
                    hit[0] = b.pos[0]; hit[1] = b.pos[1]; hit[2] = b.pos[2];
                    return 3;
                }
            }
        }
    }
    // the terrain: setCollisionData(20, 1.5) around the shot, then a first-hit segment test
    std::vector<CCPoly> polys;
    const float lo[3] = {fsub(pos[0], 20.0f), fsub(pos[1], 30.0f), fsub(pos[2], 20.0f)}, hi[3] = {fadd(pos[0], 20.0f), fadd(pos[1], 30.0f), fadd(pos[2], 20.0f)};
    for (const CCPoly& q : floor_polys_near(*dungeon_, *layout_, pos[0], pos[2], 1)) {
        bool in = true;
        for (int i = 0; i < 3 && in; ++i) {
            const float mn = std::min({q.v[0][i], q.v[1][i], q.v[2][i]}), mx = std::max({q.v[0][i], q.v[1][i], q.v[2][i]});
            if (mx < lo[i] || mn > hi[i]) in = false;
        }
        if (in) polys.push_back(q);
    }
    const int h = check_hit(polys.data(), int(polys.size()), pos, end, hit, true, 4);
    if (h < 0) {
        hit[0] = pos[0]; hit[1] = pos[1]; hit[2] = pos[2];
        return 0;
    }
    {   // a polygon of one of Dran's arena tiles (attribute 1..12): the tile's counter goes down while it is above 1
        const int tile = int(int16_t(polys[size_t(h)].attr[0] & 0xFFFFu));
        if (tile > 0 && tile < 13 && layout_->dran_state[size_t(tile - 1)] > 1) --layout_->dran_state[size_t(tile - 1)];
    }
    return 1;
}

}  // namespace dccore
