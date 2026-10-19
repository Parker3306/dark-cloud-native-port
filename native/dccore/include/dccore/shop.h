#pragma once
// The item shops and the storage ("check-in") counters: stock tables, prices and the buy / sell / store / take rules (notes/menus_shops.md sections 1-2, 4).
// Everything works on the player record (UserStatus) and the save data (the storage lives at SaveData + 0xe0a8); the screens only choose what to do.
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>
#include "dccore/elf.h"
#include "dccore/items.h"
#include "dccore/savedata.h"
#include "dccore/user_status.h"

namespace dccore {

enum class ShopResult {
    Ok = 0,
    NoMoney,        // you don't seem to have enough money
    NoSpace,        // you already have quite a bit there
    Cannot,         // you might need that one / you can't check that in
    Equipped,       // you're already equipped with that
    TooMuchGold,    // it'd be pretty dangerous to carry more money
    StorageFull,    // you can't check-in anymore items
    Nothing,        // nothing there
};

// Where an item of the player's is: kind 0 = bag item (slot = bag slot), 1 = weapon (owner + slot), 2 = attach item (slot).
struct ItemRef {
    int kind = 0, owner = 0, slot = 0;
};

class ShopTables {
public:
    static ShopTables load(const Elf& elf);
    static constexpr int kShops = 18;
    const std::vector<int>& stock(int shop) const { return stock_[size_t(std::clamp(shop, 0, kShops - 1))]; }
    int buy_price(int id) const;    // CalItemMoney(id, 0): 0 below 0x51
    int sell_price(int id) const;   // CalItemMoney(id, 1)
private:
    std::array<std::vector<int>, kShops> stock_;
    std::vector<int16_t> buy_, sell_;
};

constexpr int kGoldOff = 0x4346;
constexpr int kStorageOff = 0xe0a8;                       // CStockItem in SaveData
constexpr int kStorageItems = 60, kStorageWeapons = 30, kStorageAttach = 30;

int gold(const UserStatus& s);
// WeaponCalMoney: the table price of the weapon plus the prices of the attach items fitted in it (sell side).
int weapon_sell_price(const ItemDb& db, const ShopTables& t, const uint8_t* record);
// Price a sale would bring (minimum 1), or -1 when it may not be sold. `why` receives the refusal (Cannot / Equipped).
int sale_price(const UserStatus& s, const ItemDb& db, const ShopTables& t, const ItemRef& ref, ShopResult* why = nullptr);
// May the item be handed in to the storage / thrown away (IsEnableCharge)?
bool item_tradeable(const UserStatus& s, const ItemDb& db, const ItemRef& ref, ShopResult* why = nullptr);

ShopResult shop_buy(UserStatus& s, const ItemDb& db, const ShopTables& t, int shop, int index, GameRand& rng);
ShopResult shop_sell(UserStatus& s, const ItemDb& db, const ShopTables& t, const ItemRef& ref);

// The storage counter. `game_flag(0x30)` etc. are not needed here; the trade rules are the same as for selling.
ShopResult storage_store(UserStatus& s, SaveData& save, const ItemDb& db, const ItemRef& ref);
// kind / slot of the storage (items 0..59, weapons 0..29, attach 0..29).
ShopResult storage_take(UserStatus& s, SaveData& save, const ItemDb& db, int kind, int slot, GameRand& rng);
int storage_id(const SaveData& save, int kind, int slot);     // item / weapon / attach id stored there (-1 empty)
int storage_value(const SaveData& save, int kind, int slot);
int storage_count(const SaveData& save, int kind);

// The id of the thing the reference points at (-1 empty).
int item_ref_id(const UserStatus& s, const ItemRef& ref);

}  // namespace dccore
