#include "dccore/shots.h"
#include <algorithm>
#include <cstring>
#include "dccore/dungeon.h"
#include "dccore/gamemath.h"
#include "dccore/vu0.h"

namespace dccore {

using namespace gm;

ShotDef ShotDef::from_elf(const Elf& elf, uint32_t a) {
    ShotDef d;
    d.name = elf.cstring(a, 16);
    d.mode = elf.get<int32_t>(a + 0x10);
    d.look_at = elf.get<int32_t>(a + 0x14);
    for (int i = 0; i < 4; ++i) {
        d.speed[i] = elf.get<float>(a + 0x18 + 4u * uint32_t(i));
        d.radius[i] = elf.get<float>(a + 0x28 + 4u * uint32_t(i));
        d.motion[i] = elf.get<int16_t>(a + 0x4c + 2u * uint32_t(i));
        d.sound[i] = elf.get<int32_t>(a + 0x60 + 4u * uint32_t(i));
    }
    d.life = elf.get<int32_t>(a + 0x38);
    d.damage = elf.get<int32_t>(a + 0x3c);
    d.attr = elf.get<int32_t>(a + 0x40);
    d.kind = elf.get<int32_t>(a + 0x44);
    d.target = elf.get<int32_t>(a + 0x48);
    d.type = elf.get<int32_t>(a + 0x54);
    d.bomb_scale = elf.get<float>(a + 0x58);
    d.bomb_param = elf.get<int32_t>(a + 0x5c);
    return d;
}

ShotDef ShotDef::from_memory(const uint8_t* a) {
    auto i32 = [&](size_t o) { int32_t v; std::memcpy(&v, a + o, 4); return v; };
    auto i16 = [&](size_t o) { int16_t v; std::memcpy(&v, a + o, 2); return v; };
    auto f32 = [&](size_t o) { float v; std::memcpy(&v, a + o, 4); return v; };
    ShotDef d;
    d.name = std::string(reinterpret_cast<const char*>(a), strnlen(reinterpret_cast<const char*>(a), 16));
    d.mode = i32(0x10);
    d.look_at = i32(0x14);
    for (size_t i = 0; i < 4; ++i) {
        d.speed[i] = f32(0x18 + 4 * i);
        d.radius[i] = f32(0x28 + 4 * i);
        d.motion[i] = i16(0x4c + 2 * i);
        d.sound[i] = i32(0x60 + 4 * i);
    }
    d.life = i32(0x38);
    d.damage = i32(0x3c);
    d.attr = i32(0x40);
    d.kind = i32(0x44);
    d.target = i32(0x48);
    d.type = i32(0x54);
    d.bomb_scale = f32(0x58);
    d.bomb_param = i32(0x5c);
    return d;
}

namespace {
void scale_xyz(float s, float* v) {
    v[0] = fmul(v[0], s);
    v[1] = fmul(v[1], s);
    v[2] = fmul(v[2], s);
}
void normalize_xyz(float* v) {
    const vu0::V3 n = vu0::normalize(v);
    v[0] = n[0]; v[1] = n[1]; v[2] = n[2];
}
}  // namespace

int ShotEffect::set(const float muzzle[3], const float target[3], int owner_kind, int sub_id, int force_phase) {
    int i = -1;
    for (int k = 0; k < kInstances; ++k)
        if (!inst[size_t(k)].active) { i = k; break; }
    if (i < 0 || !loaded) return -1;
    Instance& s = inst[size_t(i)];
    const MotionState old_motion = s.ms;   // the object (a CCharacter) is reused: it still remembers the motion it played last
    s = Instance{};
    s.ms = old_motion;
    s.phase = def.motion[0] == -1 ? 1 : 0;
    if (force_phase != -1) s.phase = force_phase;
    s.pos[0] = muzzle[0]; s.pos[1] = muzzle[1]; s.pos[2] = muzzle[2]; s.pos[3] = 1.0f;
    if (def.mode == 0) {
        const vu0::V3 d = vu0::sub(target, muzzle);
        s.vel[0] = d[0]; s.vel[1] = d[1]; s.vel[2] = d[2];
        normalize_xyz(s.vel);
        scale_xyz(def.speed[std::clamp(s.phase, 0, 3)], s.vel);
    }
    const int16_t m = s.phase == 0 ? def.motion[0] : def.motion[1];
    s.rq = MotionRequest{-1.0f, s.phase == 0 ? 6u : 4u, int(m)};
    if (m >= 0 && size_t(m) < table.size()) s.ms.time = float(table[size_t(m)].start);
    s.life_counter = def.life;
    s.damage = def.damage;
    s.sub_id = sub_id;
    s.owner_kind = owner_kind;
    s.user_id = -1;
    s.cooldown = -1;
    s.active = true;
    return i;
}

void ShotEffect::off(int index) {
    for (int k = 0; k < kInstances; ++k)
        if (index == -1 || index == k) inst[size_t(k)].active = false;
}

void ShotEffect::end_all() {
    for (Instance& s : inst) {
        if (!s.active || (s.phase != 0 && s.phase != 1)) continue;
        s.phase = 2;
        const int16_t m = def.motion[2];
        if (m == -1) continue;
        s.ms.time = (m >= 0 && size_t(m) < table.size()) ? float(table[size_t(m)].start) : 0.0f;
        s.rq = MotionRequest{-1.0f, 6u, int(m)};
    }
}

std::vector<ShotDef> ShotSystem::load_table(const Elf& elf) {
    std::vector<ShotDef> table;
    for (uint32_t i = 0; i < 35; ++i) {
        const uint32_t p = elf.get<uint32_t>(0x27FA70 + 4 * i);
        if (p == 0) break;
        table.push_back(ShotDef::from_elf(elf, p));
    }
    return table;
}

int ShotSystem::entry(int def_index, const ShotDef& def, std::shared_ptr<Character> model) {
    if (def_index < 0 || !model) return -1;
    for (int i = 0; i < kSlots; ++i)
        if (slot[size_t(i)].loaded && slot[size_t(i)].def_index == def_index) return i;
    for (int i = 0; i < kSlots; ++i) {
        ShotEffect& s = slot[size_t(i)];
        if (s.loaded) continue;
        s.def = def;
        s.def_index = def_index;
        s.model = std::move(model);
        s.table.clear();
        for (const MotionEntry& m : s.model->motions) s.table.push_back({int(m.start), int(m.end), m.speed});
        s.loaded = true;
        return i;
    }
    return -1;
}

int ShotSystem::active_count() const {
    int n = 0;
    for (const ShotEffect& s : slot)
        for (const ShotEffect::Instance& i : s.inst) n += i.active ? 1 : 0;
    return n;
}

void ShotSystem::step(ShotEnv& env, Which which, int slot_index) {
    if (env.pool) env.pool->source = 1;
    for (ShotEffect& fx : slot) {
        if (!fx.loaded) continue;
        const int index = int(&fx - slot.data());
        if (which == Which::Only && index != slot_index) continue;
        if (which == Which::Except && index == slot_index) continue;
        const ShotDef& d = fx.def;
        for (ShotEffect::Instance& s : fx.inst) {
            if (!s.active) continue;
            auto motion_end = [&](int phase) {
                const int16_t m = d.motion[std::clamp(phase, 0, 3)];
                return (m >= 0 && size_t(m) < fx.table.size()) ? float(fx.table[size_t(m)].end) : -1000.0f;
            };
            auto finish = [&](const float at[3]) {
                s.active = false;
                if (d.type == 100 && env.on_bomb) env.on_bomb(d.bomb_scale, at, d.target, d.bomb_param);
            };
            auto enter_motion = [&](int phase, uint32_t flags) {
                const int16_t m = d.motion[std::clamp(phase, 0, 3)];
                s.ms.time = (m >= 0 && size_t(m) < fx.table.size()) ? float(fx.table[size_t(m)].start) : 0.0f;
                s.rq = MotionRequest{-1.0f, flags, int(m)};
            };
            const float end_time = motion_end(s.phase);
            if (s.loop != -1) {   // a looping effect (Ruby's charge): moves on to its next motion when one ends, hits nothing
                if (s.phase < s.loop && end_time - 1.0f <= s.ms.time && s.ms.time <= end_time) {
                    ++s.phase;
                    const int16_t m = d.motion[std::clamp(s.phase, 0, 3)];
                    s.ms.time = (m >= 0 && size_t(m) < fx.table.size()) ? float(fx.table[size_t(m)].start) : 0.0f;
                    s.rq = MotionRequest{-1.0f, 4u, int(m)};
                }
                continue;
            }
            if (s.phase == 0 && end_time - 1.0f <= s.ms.time && s.ms.time <= end_time) {
                s.phase = 1;
                if (d.motion[1] == -1) { s.active = false; continue; }
                enter_motion(1, 4);
            }
            float centre[4] = {s.pos[0], s.pos[1], s.pos[2], 1.0f};
            int hit = 0;
            if (s.phase < 2 && env.collide) hit = env.collide(d.radius[s.phase], s.pos, s.vel, d.target, centre);
            if (s.cooldown > 0) --s.cooldown;
            const float radius = d.radius[std::clamp(s.phase, 0, 3)];
            if (0.0f < radius && s.cooldown != 0) {
                if (s.wait_counter <= 0) {
                    const int idx = env.pool ? env.pool->set(radius, 1.0f, centre, s.damage, 2, d.target, d.kind, d.attr, 0) : -1;
                    if (idx != -1) {
                        ColEntry& c = env.pool->e[size_t(idx)];
                        c.owner_kind = s.owner_kind;
                        c.sub_id = s.sub_id;
                        c.status_flags = s.status_flags;
                        c.slayer = s.slayer;
                        c.owner_id = s.user_id;
                        c.only_monster_kind = -1;
                        if (d.kind == 3) {
                            s.vel[3] = 1.0f;
                            float dir[3];
                            if (d.speed[std::clamp(s.phase, 0, 3)] <= 0.0f) {
                                dir[0] = fsub(env.player_pos[0], s.pos[0]);
                                dir[1] = 0.0f;
                                dir[2] = fsub(env.player_pos[2], s.pos[2]);
                            } else {
                                dir[0] = s.vel[0]; dir[1] = s.vel[1]; dir[2] = s.vel[2];
                            }
                            normalize_xyz(dir);
                            c.dir[0] = dir[0]; c.dir[1] = dir[1]; c.dir[2] = dir[2];
                        }
                        s.wait_counter = s.wait;
                    }
                } else {
                    --s.wait_counter;
                }
            }
            if (hit == 0) {
                s.pos[0] = fadd(s.pos[0], s.vel[0]);
                s.pos[1] = fadd(s.pos[1], s.vel[1]);
                s.pos[2] = fadd(s.pos[2], s.vel[2]);
                if (s.phase == 1 && s.life_counter != -1 && --s.life_counter == -1) {
                    s.phase += 2;
                    if (d.motion[3] == -1) { finish(centre); continue; }
                    enter_motion(3, 6);
                    normalize_xyz(s.vel);
                    scale_xyz(d.speed[3], s.vel);
                }
            } else if (s.phase == 1) {
                s.phase = 2;
                if (d.motion[2] == -1) { finish(centre); continue; }
                enter_motion(2, 6);
                normalize_xyz(s.vel);
                scale_xyz(d.speed[2], s.vel);
            }
            if ((s.phase == 2 || s.phase == 3) && end_time - 1.0f <= s.ms.time && s.ms.time < end_time) finish(centre);
        }
    }
    if (env.pool) env.pool->source = 0;
}

void ShotSystem::step_motions(GameRand* rng) {
    for (ShotEffect& fx : slot) {
        if (!fx.loaded) continue;
        for (ShotEffect::Instance& s : fx.inst) {
            if (!s.active) continue;
            if (0.0f <= s.random_rate && rng)
                for (int k = 0; k < 3; ++k) rng->next();   // Draw: the jitter of the effect's position
            int phase = 0;
            step_motion_state(fx.table, s.rq, s.ms, phase);
        }
    }
}

}  // namespace dccore
