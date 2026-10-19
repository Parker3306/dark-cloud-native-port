#pragma once
// Weapon records in the menus: the derived stats the combat uses (WeaponAllValueSet), attaching synthesis items, level up, status break, build-up, repair,
// elements, equip and discard. Rules and formulas: notes/menus.md sections C.3, C.4 and D. The records are the 0xf8-byte WEAPON_HAVE of UserStatus.
#include <cstdint>
#include <vector>
#include "dccore/items.h"
#include "dccore/user_status.h"

namespace dccore {

// After this call every UserStatus::weapon(k) view shows the derived stats (stored base + attachments, SetWeaponAttachStatus); `stored_weapon(k)` the record itself.
void set_weapon_rules(const ItemDb* db);

// WeaponAllValueSet(src, dst, force_rate_1): dst = src with the derived Attack / Endurance / Speed / Magic, element and slayer points and flags.
void weapon_all_value_set(const ItemDb& db, const uint8_t* src, uint8_t* dst, bool force_rate_1);
// CheckWeaponOptionStatus: the exclusive pairs of status-effect bits cancel each other.
uint16_t check_weapon_option_status(uint16_t flags);

constexpr int kWeaponSlots = 10;
int default_weapon_of(int ch);                 // defWeapon[ch] (the broken default; +1 is the usable one)
bool is_default_weapon(int id, int* owner = nullptr);

enum class WeaponResult {
    Ok = 0,
    Cursed,            // cannot disarm / change arms due to spell (411)
    LimitedZone,       // weapon limited zone (412)
    NotInParty,        // no characters who can arm this weapon (163)
    NotAllowed,        // generic refusal
    BrokenDefault,     // (170)
    MaxLevel,          // (169)
    NeedsPowder,
    NothingToDo,
    AttachFull,        // (162)
    HasAttachments,    // build-up not allowed with attachments (168)
    ZeroElement,       // attribute with 0 pt cannot be activated (161)
    ElementRequired,   // attribute cannot be removed (171)
    SerpentSword,      // (167 / 413)
    DefaultWeapon,     // default weapons cannot be discarded (166)
    NotBuildable,
};

WeaponResult weapon_equip(UserStatus& s, int ch, int slot);
WeaponResult weapon_discard(UserStatus& s, const ItemDb& db, int ch, int slot, bool serpent_flag);
// Choose the element a weapon shows (0..4); the active one again clears it (5). Needs points in the derived stats.
WeaponResult weapon_set_element(UserStatus& s, const ItemDb& db, int ch, int slot, int element);
// Customize: put the attach item `attach_slot` of the list into hole `hole` (swapping with what the hole held, which goes back to the list slot).
WeaponResult weapon_attach(UserStatus& s, const ItemDb& db, int ch, int slot, int hole, int attach_slot);
// Take the attachment out of a hole into the first free attach slot.
WeaponResult weapon_detach(UserStatus& s, const ItemDb& db, int ch, int slot, int hole);
WeaponResult weapon_repair(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot, bool consume_powder);          // Repair Powder (0xb1) on the weapon
WeaponResult weapon_level_up(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot, bool use_powder, bool serpent_flag);   // Upgrade / Powerup Powder (0xb2)
WeaponResult weapon_status_break(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot);

struct BuildTarget {
    int id = 0;
    bool buildable = false;
};
std::vector<BuildTarget> build_up_targets(const UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot);
WeaponResult weapon_build_up(UserStatus& s, const ItemDb& db, GameRand& rng, int ch, int slot, int target_id);

// The effect of a trap circle (Run_TrapCircle, kinds 0..9; message 300 + kind in meswin/sysarticle): the changes to the record are made here, the two that
// concern other objects are reported.
struct TrapResult {
    bool stamina = false;         // kind 0: the Stamina state (BtSetStatusErr(8))
    bool pump_monsters = false;   // kind 5: every monster's bin2 becomes 300
};
TrapResult trap_circle_effect(UserStatus& s, const ItemDb& db, GameRand& rng, int kind);

// How many of the bag's items of that id (0xb1 / 0xb2 ...) the player holds (bag and quick slots).
int bag_count(const UserStatus& s, int id);
void bag_take_one(UserStatus& s, int id);

}  // namespace dccore
