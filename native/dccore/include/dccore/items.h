#pragma once
// Item database (the tables in the game's data segment: ComItemInfo, ITEM_LIST, AttachList, WeaponList) and the player's bag (UserStatus), ported from
// CDngStatusData::GetItem / CheckItemGet, ItemDataToHaveCopy, WepDataListToHaveCopy, SetAttachMentValue, GetWeaponMaxExp.
#include <array>
#include <cstdint>
#include <vector>
#include "dccore/bytes.h"
#include "dccore/dungeon.h"
#include "dccore/elf.h"
#include "dccore/user_status.h"

namespace dccore {

class ItemDb {
public:
    static ItemDb load(const Elf& elf);

    struct Info { int type = -1; int index = -1; };   // type 0 attach (synthesis material), 1 item, 2 weapon
    Info info(int id) const;                          // GetItemTypeInfo
    int icon_cell(int id) const;                      // the cell of the item / weapon icon sheet (ComItemInfo short +4), -1 if none
    int name_message(int id) const;                   // GetWeaponMsgNo + 100: the system-text message with the item's / weapon's name
    const uint8_t* item(int id) const;                // ITEM_LIST entry (0x14 bytes) or null
    const uint8_t* attach(int id) const;              // AttachList entry (0x20 bytes) or null
    const uint8_t* weapon(int id) const;              // WeaponList entry (0x4c bytes) or null
    int weapon_max_exp(const uint8_t* weapon_have) const;   // GetWeaponMaxExp of a 0xf8-byte WEAPON_HAVE record

    // ItemDataToHaveCopy: the value stored next to a bag item (uses, or minutes for some); draws rand() for a few items.
    int item_value(int id, GameRand& rng) const;
    // WepDataListToHaveCopy: fills a 0xf8-byte weapon record from the table.
    void make_weapon(int id, uint8_t* record, GameRand& rng) const;

private:
    std::vector<uint8_t> info_, items_, attach_, weapons_;
};

constexpr int kBagSlots = 0x67;

// CDngStatusData::CheckItemGet: 0 = there is room, 1 = bag full (normal items), 2 = attach list full.
int check_item_get(const UserStatus& s, int id);
// CDngStatusData::GetItem: puts an item/attach/weapon into the record; returns the slot, -1 when nothing was stored.
int get_item(UserStatus& s, const ItemDb& db, int id, GameRand& rng, int attach_value = 1);

}  // namespace dccore
