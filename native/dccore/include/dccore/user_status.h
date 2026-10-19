#pragma once
// The game's big player record (`UserStatus`, CDngStatusData / CUserStatus): party, weapons, items, flags. Kept in the game's own byte layout so that code ported
// from the decompiled functions can use the same offsets; named helpers exist for the fields understood so far. A snapshot of the live record
// (tools/dc/snapshot_status.py) can be loaded for tests; a new game starts from defaults (to be filled in with Milestone 5, the menus/save).
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace dccore {

// One weapon the player owns (WEAPON_HAVE, 0xf8 bytes).
struct WeaponView {
    const uint8_t* p;
    int16_t s16(size_t off) const { int16_t v; std::memcpy(&v, p + off, 2); return v; }
    int8_t s8(size_t off) const { return int8_t(p[off]); }
    float f32(size_t off) const { float v; std::memcpy(&v, p + off, 4); return v; }
    int16_t id() const { return s16(0); }
    int16_t level() const { return s16(2); }          // how many times it was built up
    int16_t attack() const { return s16(4); }         // +4: the attack value the swing damage is based on
    int16_t max_durability() const { return s16(0xc); }   // +0x0c (UserStatus +0x4518)
    float durability() const { return f32(0x10); }    // +0x10
    int16_t exp() const { return s16(0x14); }
    int8_t element_choice() const { return s8(0x16); }    // +0x16: which element the weapon shows (index into element_tbl)
    int8_t element_value(int i) const { return s8(0x17 + size_t(i)); }   // +0x17..: element points (fire, ice, thunder, wind, holy)
    const int8_t* slayer() const { return reinterpret_cast<const int8_t*>(p + 0x1c); }   // +0x1c: bonus against each kind of monster
    int16_t status_flags() const { return s16(0xee); }    // +0xee: poison, stop, ... effects of the weapon
    int16_t recharge() const { return s16(8); }           // +0x08: how fast the attack gauge refills
    int16_t magic() const { return s16(0xa); }            // +0x0a (UserStatus +0x4516): used with element points in the damage formula
};

struct UserStatus {
    static constexpr size_t kSize = 0x9000;
    std::array<uint8_t, kSize> raw{};

    template <class T>
    T get(size_t off) const { T v; std::memcpy(&v, raw.data() + off, sizeof(T)); return v; }
    template <class T>
    void set(size_t off, T v) { std::memcpy(raw.data() + off, &v, sizeof(T)); }

    int character() const { return get<int8_t>(4); }       // the character being played (0 Toan, 1 Xiao, 2 Goro, 3 Ruby, 4 Ungaga, 5 Osmond)
    int floor() const { return get<int8_t>(2); }
    int16_t life(int k) const { return get<int16_t>(0x12 + size_t(k) * 2); }
    void set_life(int k, int16_t v) { set<int16_t>(0x12 + size_t(k) * 2, v); }
    int16_t max_life(int k) const { return get<int16_t>(6 + size_t(k) * 2); }
    int32_t defense(int k) const { return get<int32_t>(0x4348 + size_t(k) * 4); }
    int current_weapon_slot(int k) const { return get<int8_t>(0x4340 + size_t(k)); }
    size_t weapon_offset(int k) const { return 0x450c + size_t(k) * 0xaa8 + size_t(current_weapon_slot(k)) * 0xf8; }
    // The weapon record the combat reads: the stored base values plus the attachments (WeaponAllValueSet), once weapon rules are installed (weapon_ops.h).
    // The view points into a cache that is recomputed at every call; the record itself is `stored_weapon`.
    using DeriveFn = void (*)(const uint8_t* src, uint8_t* dst);
    static inline DeriveFn derive_weapon = nullptr;
    mutable std::array<std::array<uint8_t, 0xf8>, 6> derived_{};
    WeaponView stored_weapon(int k) const { return WeaponView{raw.data() + weapon_offset(k)}; }
    WeaponView weapon(int k) const {
        if (!derive_weapon || k < 0 || k > 5) return stored_weapon(k);
        derive_weapon(raw.data() + weapon_offset(k), derived_[size_t(k)].data());
        return WeaponView{derived_[size_t(k)].data()};
    }
    int dungeon() const { return get<int8_t>(0); }
    // The record at the start of a new game (values read from the live game at the first floor with the first weapon): Toan with 70 life and the Atlamillia-less
    // starting sword. Everything else (items, party flags, quests) is left empty until the menus/save work (Milestone 5) fills in the real initialisation.
    static UserStatus new_game() {
        UserStatus s;
        s.set<int8_t>(5, 1);
        constexpr int16_t kMaxLife[6] = {70, 60, 100, 90, 110, 100};
        constexpr int32_t kDefense[6] = {3, 1, 12, 23, 38, 46};
        for (int k = 0; k < 6; ++k) {
            s.set<int16_t>(6 + size_t(k) * 2, kMaxLife[k]);
            s.set<int16_t>(0x12 + size_t(k) * 2, kMaxLife[k]);
            s.set<int32_t>(0x4348 + size_t(k) * 4, kDefense[k]);
            s.set<int8_t>(0x4340 + size_t(k), k == 0 ? int8_t(0) : int8_t(-1));
        }
        for (int k = 0; k < 6; ++k)
            for (int slot = 0; slot < 11; ++slot) s.set<int16_t>(0x450c + size_t(k) * 0xaa8 + size_t(slot) * 0xf8, -1);   // empty weapon slots
        // the lists of atra statues per dungeon / floor and the dungeon table start out as "none" (-1); the flag per dungeon says the table was not made yet
        for (size_t off = 0x278; off < 0x2078 + 6 * 0x4b0; off += 4) s.set<int32_t>(off, -1);
        for (size_t m = 0; m < 7; ++m) s.set<int8_t>(0x42bf + m, -1);
        for (size_t i = 0; i < 0x67; ++i) s.set<int16_t>(0x436e + i * 2, -1);   // empty bag
        for (size_t k = 0; k < 6; ++k) {   // the water gauge
            s.set<float>(0x42ec + k * 4, 30.0f);
            s.set<float>(0x4304 + k * 4, 30.0f);
        }
        s.set<int32_t>(0x8b10, -1);
        s.set<int32_t>(0x433c, 1);                                                // map display density: the second setting
        s.set<int8_t>(0x4360, 50);                                                // bag capacity (value seen in the live game)
        for (size_t i = 0; i < 3; ++i) s.set<int16_t>(0x4362 + i * 2, -1);        // no active (quick-use) items
        for (size_t i = 0; i < 0x2b; ++i) s.set<int16_t>(0x84fc + i * 0x20, -1);   // empty attach list
        // Toan's first weapon (id 258): the non-zero bytes of its record
        const size_t w = 0x450c;
        constexpr uint8_t kSword[0x22] = {2, 1, 1, 0, 7, 0, 31, 0, 71, 0, 3, 0, 49, 0, 0, 0, 0x39, 0x8f, 0x9a, 0x40, 3, 0, 3, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 4};
        for (size_t i = 0; i < sizeof kSword; ++i) s.raw[w + i] = kSword[i];
        s.set<int16_t>(w + 0xee, 1);
        return s;
    }
    // kills are counted per dungeon and floor (+0x3c98 + 200 * dungeon + 2 * floor)
    void add_kill() {
        const size_t off = 0x3c98 + size_t(dungeon()) * 200 + size_t(floor()) * 2;
        set<int16_t>(off, int16_t(get<int16_t>(off) + 1));
    }
};

}  // namespace dccore
