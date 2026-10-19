#include "dccore/weapon_ops.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dccore {

namespace {

const ItemDb* g_db = nullptr;

constexpr int kDefault[6] = {257, 299, 314, 331, 347, 363};
constexpr size_t kAttachBase = 0x28, kAttachStride = 0x20;   // the six attachment records inside a weapon record
constexpr int kMaxBuild[6] = {42, 15, 17, 16, 16, 14};       // MenuCharaWeaponMax

int16_t rd16(const uint8_t* p, size_t o) { int16_t v; std::memcpy(&v, p + o, 2); return v; }
void wr16(uint8_t* p, size_t o, int v) { const int16_t x = int16_t(v); std::memcpy(p + o, &x, 2); }
int rd8(const uint8_t* p, size_t o) { return int(int8_t(p[o])); }

size_t rec_off(int ch, int slot) { return 0x450c + size_t(ch) * 0xaa8 + size_t(slot) * 0xf8; }
uint8_t* rec(UserStatus& s, int ch, int slot) { return s.raw.data() + rec_off(ch, slot); }
const uint8_t* rec(const UserStatus& s, int ch, int slot) { return s.raw.data() + rec_off(ch, slot); }
uint8_t* attach_rec(UserStatus& s, int i) { return s.raw.data() + 0x84fc + size_t(i) * 0x20; }

int hole_type(const uint8_t* row, int k) { return row ? int(row[0xb + k]) : 0; }
int cap_attack(const uint8_t* row) { const int v = row ? rd16(row, 0x44) : 0; return v > 0 ? v : 999; }
int cap_magic(const uint8_t* row) { const int v = row ? rd16(row, 0x46) : 0; return v > 0 ? v : 999; }

struct Acc {
    int stat[4] = {0, 0, 0, 0};    // attack, endurance, speed, magic
    int elem[5] = {0, 0, 0, 0, 0};
    int slay[10] = {};
    uint16_t flags = 0;
    int fusion = 0;
};

// AttachMentValuePlus: add one attachment (times the multiplier) to the running sums, capping as it goes
void add_attachment(Acc& a, const uint8_t* at, int mult, const uint8_t* row) {
    const int caps[4] = {cap_attack(row), 99, 99, cap_magic(row)};
    for (int i = 0; i < 4; ++i) a.stat[i] = std::min(a.stat[i] + rd16(at, 8 + size_t(i) * 2) * mult, caps[i]);
    for (int i = 0; i < 5; ++i) a.elem[i] = std::min(a.elem[i] + rd8(at, 0x10 + size_t(i)) * mult, 99);
    for (int i = 0; i < 10; ++i) a.slay[i] = std::min(a.slay[i] + rd8(at, 0x15 + size_t(i)) * mult, 99);
}

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

void derive_cb(const uint8_t* src, uint8_t* dst) {
    if (!g_db || rd16(src, 0) < 0x101) { std::memcpy(dst, src, 0xf8); return; }
    weapon_all_value_set(*g_db, src, dst, false);
}

int free_attach_slot(const UserStatus& s) {
    for (int i = 0; i < 0x2b; ++i) if (s.get<int16_t>(0x84fc + size_t(i) * 0x20) < 0x51) return i;
    return -1;
}

int attach_count(const UserStatus& s) {
    int n = 0;
    for (int i = 0; i < 0x28; ++i) { const int v = s.get<int16_t>(0x84fc + size_t(i) * 0x20); if (v > 0x50 && v < 0x84) ++n; }
    return n;
}

void clear_attach(UserStatus& s, int i) {
    std::memset(attach_rec(s, i), 0, 0x20);
    wr16(attach_rec(s, i), 0, -1);
}

}  // namespace

void set_weapon_rules(const ItemDb* db) {
    g_db = db;
    UserStatus::derive_weapon = db ? &derive_cb : nullptr;
}

int default_weapon_of(int ch) { return ch >= 0 && ch < 6 ? kDefault[ch] : -1; }

bool is_default_weapon(int id, int* owner) {
    for (int c = 0; c < 6; ++c)
        if (id == kDefault[c] || id == kDefault[c] + 1) { if (owner) *owner = c; return true; }
    return false;
}

uint16_t check_weapon_option_status(uint16_t f) {
    static const uint16_t pairs[4][2] = {{2, 4}, {8, 0x10}, {0x100, 0x200}, {0x400, 0x800}};
    for (const auto& p : pairs)
        if ((f & p[0]) && (f & p[1])) f = uint16_t(f & ~(p[0] | p[1]));
    return f;
}

void weapon_all_value_set(const ItemDb& db, const uint8_t* src, uint8_t* dst, bool force_rate_1) {
    std::memcpy(dst, src, 0xf8);
    const int id = rd16(src, 0);
    const uint8_t* row = db.weapon(id);
    if (!row) return;
    float rate = 1.0f;
    if (!force_rate_1 && id == 0x110) {   // Lamb's Sword hits harder when it is almost broken
        float cur;
        std::memcpy(&cur, src + 0x10, 4);
        if (cur <= 0.2f * float(rd16(src, 0xc))) rate = 1.5f;
    }
    Acc a;
    for (int k = 0; k < 6; ++k) {
        if (hole_type(row, k) <= 0) continue;
        const uint8_t* at = src + kAttachBase + size_t(k) * kAttachStride;
        const int aid = rd16(at, 0);
        if (aid < 0x51 || aid > 0x83) continue;
        const int mult = src[0xe8 + k] == 3 ? 2 : 1;
        for (int i = 0; i < 4; ++i) a.stat[i] += rd16(at, 8 + size_t(i) * 2) * mult;
        for (int i = 0; i < 5; ++i) a.elem[i] += rd8(at, 0x10 + size_t(i)) * mult;
        for (int i = 0; i < 10; ++i) a.slay[i] += rd8(at, 0x15 + size_t(i)) * mult;
        if (aid == 0x5a) {
            const uint16_t f = uint16_t(rd16(at, 4));
            if (f > 1) a.flags |= f;
        }
    }
    auto scaled = [&](int v) { return int(std::trunc(float(v) * rate)); };
    wr16(dst, 4, std::min(int(std::trunc(float(cap_attack(row)) * rate)), scaled(rd16(src, 4) + a.stat[0])));
    wr16(dst, 6, std::min(99, scaled(rd16(src, 6) + a.stat[1])));
    wr16(dst, 8, std::min(99, scaled(rd16(src, 8) + a.stat[2])));
    wr16(dst, 10, std::min(int(std::trunc(float(cap_magic(row)) * rate)), scaled(rd16(src, 10) + a.stat[3])));
    for (int i = 0; i < 5; ++i) dst[0x17 + i] = uint8_t(int8_t(std::min(99, scaled(rd8(src, 0x17 + size_t(i)) + a.elem[i]))));
    for (int i = 0; i < 10; ++i) dst[0x1c + i] = uint8_t(int8_t(std::min(99, scaled(rd8(src, 0x1c + size_t(i)) + a.slay[i]))));
    wr16(dst, 0xee, check_weapon_option_status(uint16_t(uint16_t(rd16(src, 0xee)) | a.flags)));
}

int bag_count(const UserStatus& s, int id) {
    int n = 0;
    for (int i = 0; i < kBagSlots; ++i) if (s.get<int16_t>(0x436e + size_t(i) * 2) == id) ++n;
    for (int i = 0; i < 3; ++i) if (s.get<int16_t>(0x4362 + size_t(i) * 2) == id) n += s.get<int16_t>(0x4368 + size_t(i) * 2);
    return n;
}

void bag_take_one(UserStatus& s, int id) {
    for (int i = 0; i < kBagSlots; ++i)
        if (s.get<int16_t>(0x436e + size_t(i) * 2) == id) {
            s.set<int16_t>(0x436e + size_t(i) * 2, -1);
            s.set<int16_t>(0x443c + size_t(i) * 2, 0);
            return;
        }
    for (int i = 0; i < 3; ++i)
        if (s.get<int16_t>(0x4362 + size_t(i) * 2) == id) {
            const int c = s.get<int16_t>(0x4368 + size_t(i) * 2) - 1;
            s.set<int16_t>(0x4368 + size_t(i) * 2, int16_t(std::max(c, 0)));
            if (c <= 0) s.set<int16_t>(0x4362 + size_t(i) * 2, -1);
            return;
        }
}

WeaponResult weapon_equip(UserStatus& s, int ch, int slot) {
    if (ch < 0 || ch > 5 || slot < 0 || slot >= kWeaponSlots || rd16(rec(s, ch, slot), 0) < 0x101) return WeaponResult::NotAllowed;
    if (s.get<uint32_t>(0x42c8 + size_t(ch) * 4) & 0x20) return WeaponResult::Cursed;
    if (s.get<int32_t>(0x8b10) == 10) return WeaponResult::LimitedZone;
    if (ch >= s.get<int8_t>(5)) return WeaponResult::NotInParty;
    s.set<int8_t>(0x4340 + size_t(ch), int8_t(slot));
    return WeaponResult::Ok;
}

WeaponResult weapon_discard(UserStatus& s, const ItemDb& db, int ch, int slot, bool serpent_flag) {
    (void)db;
    uint8_t* w = rec(s, ch, slot);
    const int id = rd16(w, 0);
    if (id < 0x101) return WeaponResult::NotAllowed;
    if (is_default_weapon(id)) return WeaponResult::DefaultWeapon;
    if (id == 0x10c && !serpent_flag) return WeaponResult::SerpentSword;
    std::memset(w, 0, 0xf8);
    wr16(w, 0, -1);
    if (s.current_weapon_slot(ch) == slot) {   // the default weapon takes over
        for (int i = 0; i < kWeaponSlots; ++i)
            if (rd16(rec(s, ch, i), 0) == kDefault[ch] + 1) { s.set<int8_t>(0x4340 + size_t(ch), int8_t(i)); break; }
    }
    return WeaponResult::Ok;
}

WeaponResult weapon_set_element(UserStatus& s, const ItemDb& db, int ch, int slot, int element) {
    uint8_t* w = rec(s, ch, slot);
    if (rd16(w, 0) < 0x101 || element < 0 || element > 4) return WeaponResult::NotAllowed;
    uint8_t derived[0xf8];
    weapon_all_value_set(db, w, derived, true);
    const int cur = w[0x16];
    if (cur == element) {   // clearing it: some weapons must keep an element (Ruby's)
        const int id = rd16(w, 0);
        if (ch == 3 || id == 0x170 || id == 0x171 || id == 0x173 || id == 0x175) return WeaponResult::ElementRequired;
        w[0x16] = 5;
        return WeaponResult::Ok;
    }
    if (rd8(derived, 0x17 + size_t(element)) <= 0) return WeaponResult::ZeroElement;
    w[0x16] = uint8_t(element);
    return WeaponResult::Ok;
}

static void fix_element(UserStatus& s, const ItemDb& db, int ch, int slot) {   // WeaponMenuCheckElemValue
    uint8_t* w = rec(s, ch, slot);
    uint8_t derived[0xf8];
    weapon_all_value_set(db, w, derived, true);
    const int cur = w[0x16];
    if (cur < 5 && rd8(derived, 0x17 + size_t(cur)) > 0) return;
    int best = 5, bv = 0;
    for (int e = 0; e < 5; ++e) if (rd8(derived, 0x17 + size_t(e)) > bv) { bv = rd8(derived, 0x17 + size_t(e)); best = e; }
    w[0x16] = uint8_t(best);
}

WeaponResult weapon_attach(UserStatus& s, const ItemDb& db, int ch, int slot, int hole, int attach_slot) {
    uint8_t* w = rec(s, ch, slot);
    const uint8_t* row = db.weapon(rd16(w, 0));
    if (!row || hole < 0 || hole >= 6 || hole_type(row, hole) <= 0) return WeaponResult::NotAllowed;
    if (attach_slot < 0 || attach_slot >= 0x2b) return WeaponResult::NotAllowed;
    uint8_t* from = attach_rec(s, attach_slot);
    const int id = rd16(from, 0);
    if (id < 0x51 || id > 0x83) return WeaponResult::NotAllowed;
    if (id == 0x5a && hole_type(row, hole) != 2) return WeaponResult::NotAllowed;   // a SynthSphere only goes to a plate
    uint8_t* to = w + kAttachBase + size_t(hole) * kAttachStride;
    uint8_t tmp[0x20];
    std::memcpy(tmp, to, 0x20);
    std::memcpy(to, from, 0x20);
    const int held = rd16(tmp, 0);
    if (held > 0x50 && held < 0x84) std::memcpy(from, tmp, 0x20);   // what the hole held goes back to the list
    else clear_attach(s, attach_slot);
    fix_element(s, db, ch, slot);
    return WeaponResult::Ok;
}

WeaponResult weapon_detach(UserStatus& s, const ItemDb& db, int ch, int slot, int hole) {
    uint8_t* w = rec(s, ch, slot);
    if (hole < 0 || hole >= 6) return WeaponResult::NotAllowed;
    uint8_t* at = w + kAttachBase + size_t(hole) * kAttachStride;
    const int id = rd16(at, 0);
    if (id < 0x51 || id > 0x83) return WeaponResult::NothingToDo;
    if (attach_count(s) >= 0x28) return WeaponResult::AttachFull;
    const int dst = free_attach_slot(s);
    if (dst < 0) return WeaponResult::AttachFull;
    std::memcpy(attach_rec(s, dst), at, 0x20);
    std::memset(at, 0, 0x20);
    wr16(at, 0, -1);
    fix_element(s, db, ch, slot);
    return WeaponResult::Ok;
}

WeaponResult weapon_repair(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot, bool consume_powder) {
    uint8_t* w = rec(s, ch, slot);
    const int id = rd16(w, 0);
    if (id < 0x101) return WeaponResult::NotAllowed;
    float cur;
    std::memcpy(&cur, w + 0x10, 4);
    if (!(cur < float(rd16(w, 0xc)))) return WeaponResult::NothingToDo;
    if (consume_powder && bag_count(s, 0xb1) < 1) return WeaponResult::NeedsPowder;
    if (id == kDefault[ch]) {   // the broken default becomes the real default weapon
        db.make_weapon(kDefault[ch] + 1, w, rng);
    } else {
        const float mx = float(rd16(w, 0xc));
        std::memcpy(w + 0x10, &mx, 4);
    }
    if (consume_powder) bag_take_one(s, 0xb1);
    return WeaponResult::Ok;
}

WeaponResult weapon_level_up(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot, bool use_powder, bool serpent_flag) {
    uint8_t* w = rec(s, ch, slot);
    const int id = rd16(w, 0);
    if (id < 0x101) return WeaponResult::NotAllowed;
    if (id == kDefault[ch]) return WeaponResult::BrokenDefault;
    if (rd16(w, 2) > 0x62) return WeaponResult::MaxLevel;
    if (id == 0x10c && !serpent_flag) return WeaponResult::SerpentSword;
    const uint8_t* row = db.weapon(id);
    if (!row) return WeaponResult::NotAllowed;
    const int max_exp = db.weapon_max_exp(w);
    const bool needs_powder = rd16(w, 0x14) < max_exp;
    if (needs_powder && use_powder && bag_count(s, 0xb2) < 1) return WeaponResult::NeedsPowder;
    if (needs_powder && use_powder) bag_take_one(s, 0xb2);
    else if (needs_powder) return WeaponResult::NeedsPowder;
    // 1. the attachments add up
    Acc a;
    for (int k = 0; k < 6; ++k) {
        if (hole_type(row, k) <= 0) continue;
        const uint8_t* at = w + kAttachBase + size_t(k) * kAttachStride;
        const int aid = rd16(at, 0);
        if (aid < 0x51 || aid > 0x83) continue;
        add_attachment(a, at, w[0xe8 + k] == 3 ? 2 : 1, row);
        if (aid == 0x5a) {
            ++a.fusion;
            const uint16_t f = uint16_t(rd16(at, 4));
            if (f > 1) a.flags |= f;
        }
    }
    // 2. they are added to the stored values
    wr16(w, 4, std::min(rd16(w, 4) + a.stat[0], cap_attack(row)));
    wr16(w, 6, std::min(rd16(w, 6) + a.stat[1], 99));
    wr16(w, 8, std::min(rd16(w, 8) + a.stat[2], 99));
    wr16(w, 10, std::min(rd16(w, 10) + a.stat[3], cap_magic(row)));
    for (int i = 0; i < 5; ++i) w[0x17 + i] = uint8_t(int8_t(std::min(rd8(w, 0x17 + size_t(i)) + a.elem[i], 99)));
    for (int i = 0; i < 10; ++i) w[0x1c + i] = uint8_t(int8_t(std::min(rd8(w, 0x1c + size_t(i)) + a.slay[i], 99)));
    wr16(w, 0xc, std::min(int(rd16(w, 0xc)), 99));
    wr16(w, 0xee, check_weapon_option_status(uint16_t(uint16_t(rd16(w, 0xee)) | a.flags)));
    // 3. the attachments are used up; one eligible hole may start to count double (2 %)
    for (int k = 0; k < 6; ++k) { std::memset(w + kAttachBase + size_t(k) * kAttachStride, 0, 0x20); wr16(w, kAttachBase + size_t(k) * kAttachStride, -1); }
    bool doubled = false;
    for (int k = 0; k < 6; ++k) {
        const int t = hole_type(row, k);
        if (t <= 0 || t == 2) continue;
        if (doubled) { w[0xe8 + k] = 0; continue; }
        if (rng.next() % 50 == 5) { w[0xe8 + k] = 3; doubled = true; }
        else w[0xe8 + k] = 0;
    }
    // 4. the level itself
    wr16(w, 4, std::min(rd16(w, 4) + 1, cap_attack(row)));
    wr16(w, 6, std::min(rd16(w, 6) + 1, 99));
    wr16(w, 8, std::min(rd16(w, 8) + 1, 99));
    wr16(w, 10, std::min(rd16(w, 10) + 1, cap_magic(row)));
    wr16(w, 0xc, std::min(int(rd16(w, 0xc)) + 1 + int(rng.next() % 3), 99));
    wr16(w, 0x14, 0);
    wr16(w, 2, rd16(w, 2) + 1);
    int32_t fus;
    std::memcpy(&fus, w + 0xf0, 4);
    fus += a.fusion;
    std::memcpy(w + 0xf0, &fus, 4);
    fix_element(s, db, ch, slot);
    return WeaponResult::Ok;
}

WeaponResult weapon_status_break(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot) {
    uint8_t* w = rec(s, ch, slot);
    const int id = rd16(w, 0);
    if (id < 0x101) return WeaponResult::NotAllowed;
    if (attach_count(s) >= 0x28) return WeaponResult::AttachFull;
    const int dst = free_attach_slot(s);
    const uint8_t* table = db.attach(0x5a);
    if (dst < 0 || !table) return WeaponResult::AttachFull;
    const uint8_t* row = db.weapon(id);
    uint8_t* sph = attach_rec(s, dst);
    std::memcpy(sph, table, 0x20);
    wr16(sph, 2, id);
    wr16(sph, 6, rd16(w, 2));
    Acc a;
    uint16_t flags = uint16_t(rd16(w, 0xee));
    for (int k = 0; k < 6; ++k) {
        if (hole_type(row, k) <= 0) continue;
        const uint8_t* at = w + kAttachBase + size_t(k) * kAttachStride;
        const int aid = rd16(at, 0);
        if (aid < 0x51 || aid > 0x83) continue;
        add_attachment(a, at, w[0xe8 + k] == 3 ? 2 : 1, row);
        if (aid == 0x5a) { const uint16_t f = uint16_t(rd16(at, 4)); if (f > 1) flags |= f; }
    }
    auto sixty = [](int v) { return int(std::trunc(0.6f * float(v))); };
    wr16(sph, 8, sixty(rd16(w, 4)) + sixty(a.stat[0]));
    wr16(sph, 10, sixty(rd16(w, 6)) + sixty(a.stat[1]));
    wr16(sph, 12, sixty(rd16(w, 8)) + sixty(a.stat[2]));
    wr16(sph, 14, sixty(rd16(w, 10)) + sixty(a.stat[3]));
    for (int i = 0; i < 5; ++i) sph[0x10 + i] = uint8_t(int8_t(std::min(99, sixty(rd8(w, 0x17 + size_t(i))) + sixty(a.elem[i]))));
    for (int i = 0; i < 10; ++i) sph[0x15 + i] = uint8_t(int8_t(std::min(99, sixty(rd8(w, 0x1c + size_t(i))) + sixty(a.slay[i]))));
    wr16(sph, 4, check_weapon_option_status(flags));
    if (is_default_weapon(id)) {   // the default weapons start over at level 0
        int owner = 0;
        is_default_weapon(id, &owner);
        db.make_weapon(id, w, rng);
    } else {
        const bool equipped = s.current_weapon_slot(ch) == slot;
        std::memset(w, 0, 0xf8);
        wr16(w, 0, -1);
        if (equipped)
            for (int i = 0; i < kWeaponSlots; ++i)
                if (rd16(rec(s, ch, i), 0) == kDefault[ch] + 1) { s.set<int8_t>(0x4340 + size_t(ch), int8_t(i)); break; }
    }
    return WeaponResult::Ok;
}

static uint32_t rd32(const uint8_t* p, size_t o) { uint32_t v; std::memcpy(&v, p + o, 4); return v; }

// EnableBuildUpModel: (k > 20 or bit k of the mask at +0x3c) and (k <= 20 or bit k - 20 of the mask at +0x40)
static bool target_candidate(const uint8_t* row, int k) {
    const bool a = k > 20 || ((rd32(row, 0x3c) >> k) & 1u) != 0;
    const bool b = k <= 20 || ((rd32(row, 0x40) >> (k - 20)) & 1u) != 0;
    return a && b;
}

std::vector<BuildTarget> build_up_targets(const UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot) {
    std::vector<BuildTarget> out;
    const uint8_t* w = rec(s, ch, slot);
    const int id = rd16(w, 0);
    const uint8_t* row = db.weapon(id);
    if (!row || ch < 0 || ch > 5) return out;
    uint8_t derived[0xf8];
    weapon_all_value_set(db, w, derived, true);
    for (int k = 2; k <= kMaxBuild[ch]; ++k) {
        const int tid = kDefault[ch] + k;
        if (!target_candidate(row, k)) continue;
        const uint8_t* trow = db.weapon(tid);
        if (!trow) continue;
        BuildTarget t;
        t.id = tid;
        // CompareBuildUpModelData2: Attack, Magic, five elements, ten slayers: a positive requirement must be reached, a negative one is an upper bound
        auto ok = [](int req, int have) { return req >= 0 ? req <= have : have <= -req; };
        uint8_t proto[0xf8];
        GameRand tmp = rng;
        db.make_weapon(tid, proto, tmp);
        bool good = ok(rd16(proto, 4), rd16(derived, 4)) && ok(rd16(proto, 10), rd16(derived, 10));
        for (int e = 0; e < 5 && good; ++e) good = ok(rd8(proto, 0x17 + size_t(e)), rd8(derived, 0x17 + size_t(e)));
        for (int e = 0; e < 10 && good; ++e) good = ok(rd8(proto, 0x1c + size_t(e)), rd8(derived, 0x1c + size_t(e)));
        t.buildable = good;
        out.push_back(t);
    }
    return out;
}

WeaponResult weapon_build_up(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot, int target_id) {
    uint8_t* w = rec(s, ch, slot);
    if (rd16(w, 0) < 0x101) return WeaponResult::NotAllowed;
    const uint8_t* row = db.weapon(rd16(w, 0));
    for (int k = 0; k < 6; ++k)
        if (hole_type(row, k) > 0 && rd16(w, kAttachBase + size_t(k) * kAttachStride) > 0x50) return WeaponResult::HasAttachments;
    bool found = false;
    for (const BuildTarget& t : build_up_targets(s, db, rng, ch, slot))
        if (t.id == target_id) { if (!t.buildable) return WeaponResult::NotBuildable; found = true; }
    if (!found) return WeaponResult::NotAllowed;
    uint8_t proto[0xf8];
    db.make_weapon(target_id, proto, rng);
    wr16(w, 6, std::max(rd16(w, 6), rd16(proto, 6)));
    wr16(w, 8, std::max(rd16(w, 8), rd16(proto, 8)));
    wr16(w, 0xc, std::max(rd16(w, 0xc), rd16(proto, 0xc)));
    wr16(w, 0, target_id);
    wr16(w, 2, 0);
    wr16(w, 0x14, 0);
    const uint8_t* trow = db.weapon(target_id);
    if (trow) wr16(w, 0xee, uint16_t(rd16(w, 0xee)) | uint16_t(rd16(trow, 0x38)));
    const float mx = float(rd16(w, 0xc));
    std::memcpy(w + 0x10, &mx, 4);
    return WeaponResult::Ok;
}

}  // namespace dccore

namespace dccore {

// WeaponDataChangeByRGate codes: 0 exp full, 1 stats worsen, 2 max WHp up, 3 max WHp down, 4 WHp full, 5 WHp / 4
TrapResult trap_circle_effect(UserStatus& s, const ItemDb& db, GameRand& rng, int kind) {
    TrapResult r;
    const int ch = s.character();
    uint8_t* w = s.raw.data() + s.weapon_offset(ch);
    const int id = int(int16_t(w[0] | (w[1] << 8)));
    const bool def = is_default_weapon(id);   // the broken default and the usable one
    auto rd = [&](size_t o) { return int(int16_t(w[o] | (w[o + 1] << 8))); };
    auto wr = [&](size_t o, int v) { w[o] = uint8_t(v & 0xff); w[o + 1] = uint8_t((v >> 8) & 0xff); };
    auto gold = [&]() { return int(s.get<uint16_t>(0x4346)); };
    auto roll = [&](int mod) { return int(uint32_t(rng.next()) % uint32_t(mod)); };
    switch (kind) {
        case 0: r.stamina = true; break;
        case 1: {
            const int g = gold();
            const long total = long(g) + long(int(float(g) * 1.2f)) + 10;
            s.set<uint16_t>(0x4346, uint16_t(total > 0xffff ? 0xffff : total));
            break;
        }
        case 2:
            if (id != default_weapon_of(ch) && id >= 0x101) wr(0x14, db.weapon_max_exp(w));
            break;
        case 3:
            if (!def) wr(0xc, std::min(99, rd(0xc) + roll(3) + 3));
            break;
        case 4:
            if (id == default_weapon_of(ch)) {
                db.make_weapon(default_weapon_of(ch) + 1, w, rng);
            } else if (id >= 0x101) {
                const float mx = float(rd(0xc));
                std::memcpy(w + 0x10, &mx, 4);
            }
            break;
        case 5: r.pump_monsters = true; break;
        case 6: {
            const int g = gold();
            const int left = int(float(g) - float(g) * 0.2f);
            s.set<uint16_t>(0x4346, uint16_t(left < 0 ? 0 : left));
            break;
        }
        case 7:
            if (!def && id >= 0x101) {
                const int sel = rng.next() & 3;
                const int amount = roll(3) + 2;
                static constexpr size_t kStat[4] = {4, 6, 8, 0xa};
                const int floor_v = sel == 0 ? 1 : 0;
                wr(kStat[sel], std::max(floor_v, rd(kStat[sel]) - amount));
                struct Part { size_t base; int count; int base_amt; int mod; };
                for (const Part& p : {Part{0x17, 5, 2, 3}, Part{0x1c, 3, 2, 2}, Part{0x1f, 3, 2, 2}, Part{0x22, 4, 2, 2}}) {
                    const int idx = roll(p.count);
                    const int amt = p.base_amt + roll(p.mod);
                    const int v = int(int8_t(w[p.base + size_t(idx)])) - amt;
                    w[p.base + size_t(idx)] = uint8_t(int8_t(v < 0 ? 0 : v));
                }
            }
            break;
        case 8:
            if (!def && id >= 0x101) {
                int mx = rd(0xc) - (roll(3) + 3);
                if (mx < 1) mx = 1;
                wr(0xc, mx);
                float cur;
                std::memcpy(&cur, w + 0x10, 4);
                if (float(mx) < cur) { const float f = float(mx); std::memcpy(w + 0x10, &f, 4); }
            }
            break;
        case 9:
            if (id >= 0x101) {
                float cur;
                std::memcpy(&cur, w + 0x10, 4);
                cur = cur / 4.0f;
                if (cur < 1.0f) cur = 1.0f;
                std::memcpy(w + 0x10, &cur, 4);
            }
            break;
        default: break;
    }
    return r;
}

}  // namespace dccore
