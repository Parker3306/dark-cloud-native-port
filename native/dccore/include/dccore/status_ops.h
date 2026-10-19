#pragma once
// Operations on the player record that the game spreads over CUserStatus / CDngStatusData / the item code: life and water changes, item effects, weapon switching.
#include <cstdint>
#include "dccore/items.h"
#include "dccore/user_status.h"

namespace dccore {

// CUserStatus::AddNowLife: with param != 0 the life drains toward the new value by a per-tick step (CUserStatus::Step / Walker::step_status applies it); param 0 sets it at once.
void add_now_life(UserStatus& s, float param, int ch, int16_t delta);
// CUserStatus::AddDrink: the same for the water gauge.
void add_drink(UserStatus& s, float param, int ch, int16_t delta);

// What using a bag item does (usedActiveItem): returns false for items that cannot be used this way. `poison` is set when the item poisons the character
// (the caller applies the status effect with its own timer); cured statuses are cleared in the record.
struct UseEffect {
    bool used = false;
    bool poisoned = false;
    int life = 0;      // life points added
    int water = 0;     // water added
};
UseEffect use_item_effect(UserStatus& s, const ItemDb& db, int item_id);

// A character joins the party with their first weapon (and full life); `weapon_id` 0 = the character's first weapon (defWeapon + 1).
void join_party(UserStatus& s, const ItemDb& db, int ch, GameRand& rng, int weapon_id = 0);

// Takes the item in bag slot `slot` out of the bag after use.
void consume_bag_slot(UserStatus& s, int slot);

}  // namespace dccore
