#include "dccore/status_ops.h"
#include "dccore/player.h"
#include <algorithm>
#include "dccore/gamemath.h"

namespace dccore {

void add_now_life(UserStatus& s, float param, int ch, int16_t delta) {
    const size_t life_at = 0x12 + size_t(ch) * 2, max_at = 6 + size_t(ch) * 2, target_at = 0x8a5c + size_t(ch) * 2, step_at = 0x8a68 + size_t(ch) * 2;
    if (s.get<int16_t>(step_at) != 0) {
        s.set<int16_t>(life_at, s.get<int16_t>(target_at));
        s.set<int16_t>(step_at, 0);
    }
    if (param == 0.0f) {
        int16_t life = int16_t(s.get<int16_t>(life_at) + delta);
        if (life < 1) life = 0;
        if (s.get<int16_t>(max_at) <= life) life = s.get<int16_t>(max_at);
        s.set<int16_t>(life_at, life);
        return;
    }
    int16_t target = int16_t(s.get<int16_t>(life_at) + delta);
    if (target < 1) target = 0;
    if (s.get<int16_t>(max_at) <= target) target = s.get<int16_t>(max_at);
    s.set<int16_t>(target_at, target);
    const int diff = int(target) - int(s.get<int16_t>(life_at));
    int16_t step = int16_t(gm::ftoi(gm::fmul(param, gm::fdiv(gm::itof(diff), 100.0f))));
    if (step == 0) step = diff < 0 ? int16_t(-1) : int16_t(1);
    s.set<int16_t>(step_at, step);
}

void add_drink(UserStatus& s, float param, int ch, int16_t delta) {
    const size_t cur_at = 0x4304 + size_t(ch) * 4, max_at = 0x42ec + size_t(ch) * 4, target_at = 0x8ad6 + size_t(ch) * 2, step_at = 0x8ae2 + size_t(ch) * 2;
    if (s.get<int16_t>(step_at) != 0) {
        s.set<float>(cur_at, gm::itof(s.get<int16_t>(target_at)));
        s.set<int16_t>(step_at, 0);
    }
    if (param == 0.0f) {
        float v = gm::fadd(s.get<float>(cur_at), gm::itof(delta));
        if (s.get<float>(max_at) <= v) v = s.get<float>(max_at);
        s.set<float>(cur_at, v);
        return;
    }
    int16_t target = int16_t(gm::ftoi(gm::fadd(s.get<float>(cur_at), gm::itof(delta))));
    if (target < 1) target = 0;
    if (s.get<float>(max_at) <= float(target)) target = int16_t(gm::ftoi(s.get<float>(max_at)));
    s.set<int16_t>(target_at, target);
    const float diff = gm::fsub(gm::itof(target), s.get<float>(cur_at));
    int16_t step = int16_t(gm::ftoi(gm::fmul(param, gm::fdiv(diff, 100.0f))));
    if (step == 0) step = diff < 0.0f ? int16_t(-1) : int16_t(1);
    s.set<int16_t>(step_at, step);
}

UseEffect use_item_effect(UserStatus& s, const ItemDb& db, int id) {
    UseEffect r;
    const int ch = s.character();
    if (id == 0xaa) {   // the special elixir: full life, but thirsty afterwards
        add_now_life(s, 100.0f, ch, 200);
        add_drink(s, 5.0f, ch, -20);
        r.used = true;
        r.life = 200;
        r.water = -20;
        return r;
    }
    const uint8_t* e = db.item(id);
    if (!e) return r;
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, e + o, 4); return v; };
    auto s16 = [&](size_t o) { int16_t v; std::memcpy(&v, e + o, 2); return v; };
    const uint16_t type = uint16_t(s16(8));
    const uint32_t flags = u32(4);
    if ((type & 4) != 0) {
        const bool has_life = (flags & 0x40) != 0;
        if (has_life) {
            add_now_life(s, 100.0f, ch, s16(10));
            r.life = s16(10);
            r.used = true;
        }
        if ((flags & 0x80) != 0) {
            const int16_t w = s16(10 + (has_life ? 2 : 0));
            add_drink(s, 5.0f, ch, w);
            r.water = w;
            r.used = true;
        }
    }
    if ((type & 1) != 0) {
        r.used = true;
        const size_t st_at = 0x42c8 + size_t(ch) * 4;
        if ((flags & 0x1000) != 0) r.poisoned = true;
        if ((flags & 0x20000) != 0 && (s.get<uint32_t>(st_at) & 0x40) != 0) s.set<uint32_t>(st_at, 0);
        if ((flags & 0x8000) != 0 && (s.get<uint32_t>(st_at) & 0x10) != 0) s.set<uint32_t>(st_at, 0);
        if ((flags & 0x3c000) != 0) s.set<uint32_t>(st_at, 0);
    }
    return r;
}

void consume_bag_slot(UserStatus& s, int slot) {
    if (slot < 0 || slot >= kBagSlots) return;
    s.set<int16_t>(0x436e + size_t(slot) * 2, -1);
    s.set<int16_t>(0x443c + size_t(slot) * 2, 0);
}

}  // namespace dccore

namespace dccore {

void join_party(UserStatus& s, const ItemDb& db, int ch, GameRand& rng, int weapon_id) {
    if (ch < 0 || ch > 5) return;
    const int id = weapon_id != 0 ? weapon_id : kDefWeapon[ch] + 1;
    uint8_t* rec = s.raw.data() + 0x450c + size_t(ch) * 0xaa8;
    if (s.get<int16_t>(0x450c + size_t(ch) * 0xaa8) < 0x101) {
        db.make_weapon(id, rec, rng);
        s.set<int8_t>(0x4340 + size_t(ch), 0);
    }
    s.set_life(ch, s.max_life(ch));
    int joined = 0;   // UserStatus +5: how many have joined (the character rooms of the floor generator look at it)
    for (int c = 0; c < 6; ++c)
        if (s.get<int16_t>(0x450c + size_t(c) * 0xaa8) >= 0x101) ++joined;
    if (joined > s.get<int8_t>(5)) s.set<int8_t>(5, int8_t(joined));
}

}  // namespace dccore
