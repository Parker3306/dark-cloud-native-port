#include "dccore/shop.h"
#include <algorithm>
#include <cstring>

namespace dccore {

namespace {

constexpr uint32_t kShopList = 0x292020, kShopStride = 0x28, kPriceTable = 0x291A3C;
constexpr int kMaxPriceId = 0x178;

size_t bag_at(int slot) { return 0x436e + size_t(slot) * 2; }
size_t weapon_at(int owner, int slot) { return 0x450c + size_t(owner) * 0xaa8 + size_t(slot) * 0xf8; }
size_t attach_at(int slot) { return 0x84fc + size_t(slot) * 0x20; }

int16_t rd16(const uint8_t* p) { int16_t v; std::memcpy(&v, p, 2); return v; }

int weapon_owner(const ItemDb& db, int id) {
    const uint8_t* w = db.weapon(id);
    return w ? int(int8_t(w[10])) : -1;
}

int owned_weapons(const UserStatus& s, int owner) {
    int n = 0;
    for (int i = 0; i < 10; ++i)
        if (s.get<int16_t>(weapon_at(owner, i)) > 0x100) ++n;
    return n;
}

int owned_attach(const UserStatus& s) {
    int n = 0;
    for (int i = 0; i < 0x28; ++i) {
        const int16_t v = s.get<int16_t>(attach_at(i));
        if (v > 0x50 && v < 0x84) ++n;
    }
    return n;
}

bool has_default_weapon(const UserStatus& s, int owner) {
    constexpr int kDef[6] = {257, 299, 314, 331, 347, 363};
    for (int i = 0; i < 10; ++i) {
        const int id = s.get<int16_t>(weapon_at(owner, i));
        if (id == kDef[owner] || id == kDef[owner] + 1) return true;
    }
    return false;
}

uint8_t* storage(SaveData& save) { return save.raw.data() + kStorageOff; }
const uint8_t* storage(const SaveData& save) { return save.raw.data() + kStorageOff; }

}  // namespace

ShopTables ShopTables::load(const Elf& elf) {
    ShopTables t;
    for (int s = 0; s < kShops; ++s)
        for (int i = 0; i < 20; ++i) {
            const int id = elf.get<int16_t>(kShopList + uint32_t(s) * kShopStride + uint32_t(i) * 2);
            if (id > 0x50) t.stock_[size_t(s)].push_back(id);
        }
    t.buy_.resize(size_t(kMaxPriceId) + 1);
    t.sell_.resize(size_t(kMaxPriceId) + 1);
    for (int id = 0; id <= kMaxPriceId; ++id) {
        t.buy_[size_t(id)] = elf.get<int16_t>(kPriceTable + uint32_t(id) * 4);
        t.sell_[size_t(id)] = elf.get<int16_t>(kPriceTable + uint32_t(id) * 4 + 2);
    }
    return t;
}

int ShopTables::buy_price(int id) const { return id < 0x51 || id > kMaxPriceId ? 0 : buy_[size_t(id)]; }
int ShopTables::sell_price(int id) const { return id < 0x51 || id > kMaxPriceId ? 0 : sell_[size_t(id)]; }

int gold(const UserStatus& s) { return s.get<uint16_t>(kGoldOff); }

int weapon_sell_price(const ItemDb& db, const ShopTables& t, const uint8_t* record) {
    int price = t.sell_price(rd16(record));
    for (int k = 0; k < 6; ++k) {
        const int a = rd16(record + 0x28 + size_t(k) * 0x20);
        if (a > 0x50) price += t.sell_price(a);
    }
    (void)db;
    return price;
}

int item_ref_id(const UserStatus& s, const ItemRef& ref) {
    switch (ref.kind) {
        case 0: return ref.slot >= 0 && ref.slot < kBagSlots ? s.get<int16_t>(bag_at(ref.slot)) : -1;
        case 1: return ref.owner >= 0 && ref.owner < 6 && ref.slot >= 0 && ref.slot < 11 ? s.get<int16_t>(weapon_at(ref.owner, ref.slot)) : -1;
        case 2: return ref.slot >= 0 && ref.slot < 0x2b ? s.get<int16_t>(attach_at(ref.slot)) : -1;
        default: return -1;
    }
}

bool item_tradeable(const UserStatus& s, const ItemDb& db, const ItemRef& ref, ShopResult* why) {
    auto no = [&](ShopResult r) { if (why) *why = r; return false; };
    const int id = item_ref_id(s, ref);
    if (ref.kind == 0) {
        if (id < 0x84 || id > 0x100) return no(ShopResult::Nothing);
        if (id == 0xf2 || id == 0xb9) return no(ShopResult::Cannot);
        const uint8_t* e = db.item(id);
        if (e && (rd16(e + 8) & 0x10)) return no(ShopResult::Cannot);   // key items
        return true;
    }
    if (ref.kind == 2) return id > 0x50 && id < 0x84 ? true : no(ShopResult::Nothing);
    if (id <= 0x100) return no(ShopResult::Nothing);
    constexpr int kDef[6] = {257, 299, 314, 331, 347, 363};
    const int owner = ref.owner;
    if (owner < 0 || owner > 5) return no(ShopResult::Cannot);
    const bool in_party = s.get<int8_t>(5) - 1 >= owner;
    if (in_party && !has_default_weapon(s, owner)) return no(ShopResult::Cannot);
    if (id == kDef[owner] || id == kDef[owner] + 1) return no(ShopResult::Cannot);   // the default weapon and its broken twin stay
    if (id == 0x10c) return no(ShopResult::Cannot);                                   // the Serpent Sword (without its story flag; the caller checks the flag)
    if (s.current_weapon_slot(owner) == ref.slot) return no(ShopResult::Equipped);
    return true;
}

int sale_price(const UserStatus& s, const ItemDb& db, const ShopTables& t, const ItemRef& ref, ShopResult* why) {
    if (!item_tradeable(s, db, ref, why)) return -1;
    int price = 0;
    const int id = item_ref_id(s, ref);
    if (ref.kind == 1) price = weapon_sell_price(db, t, s.raw.data() + weapon_at(ref.owner, ref.slot));
    else price = t.sell_price(id);
    return std::max(price, 1);
}

ShopResult shop_buy(UserStatus& s, const ItemDb& db, const ShopTables& t, int shop, int index, GameRand& rng) {
    const auto& stock = t.stock(shop);
    if (index < 0 || size_t(index) >= stock.size()) return ShopResult::Nothing;
    const int id = stock[size_t(index)];
    const int price = t.buy_price(id);
    if (gold(s) < price) return ShopResult::NoMoney;
    if (id > 0x100) {
        const int owner = weapon_owner(db, id);
        if (owner < 0 || owned_weapons(s, owner) >= 10) return ShopResult::NoSpace;
    } else if (check_item_get(s, id) != 0) {
        return ShopResult::NoSpace;
    }
    if (get_item(s, db, id, rng) < 0) return ShopResult::NoSpace;
    s.set<uint16_t>(kGoldOff, uint16_t(gold(s) - price));
    return ShopResult::Ok;
}

ShopResult shop_sell(UserStatus& s, const ItemDb& db, const ShopTables& t, const ItemRef& ref) {
    ShopResult why = ShopResult::Ok;
    const int price = sale_price(s, db, t, ref, &why);
    if (price < 0) return why;
    if (gold(s) + price > 0xffff) return ShopResult::TooMuchGold;
    switch (ref.kind) {
        case 0: s.set<int16_t>(bag_at(ref.slot), -1); s.set<int16_t>(0x443c + size_t(ref.slot) * 2, 0); break;
        case 1: std::memset(s.raw.data() + weapon_at(ref.owner, ref.slot), 0, 0xf8); s.set<int16_t>(weapon_at(ref.owner, ref.slot), -1); break;
        default: std::memset(s.raw.data() + attach_at(ref.slot), 0, 0x20); s.set<int16_t>(attach_at(ref.slot), -1); break;
    }
    s.set<uint16_t>(kGoldOff, uint16_t(gold(s) + price));
    return ShopResult::Ok;
}

// ---- storage ---------------------------------------------------------------------------------------------------------------------------------------

int storage_id(const SaveData& save, int kind, int slot) {
    const uint8_t* b = storage(save);
    switch (kind) {
        case 0: return slot >= 0 && slot < kStorageItems ? rd16(b + size_t(slot) * 2) : -1;
        case 1: return slot >= 0 && slot < kStorageWeapons ? rd16(b + 0xf0 + size_t(slot) * 0xf8) : -1;
        default: return slot >= 0 && slot < kStorageAttach ? rd16(b + 0x1e00 + size_t(slot) * 0x20) : -1;
    }
}

int storage_value(const SaveData& save, int kind, int slot) {
    if (kind != 0 || slot < 0 || slot >= kStorageItems) return 0;
    return rd16(storage(save) + 0x78 + size_t(slot) * 2);
}

int storage_count(const SaveData& save, int kind) {
    const int n = kind == 0 ? kStorageItems : kind == 1 ? kStorageWeapons : kStorageAttach;
    const int lo = kind == 0 ? 0x84 : kind == 1 ? 0x101 : 0x51;
    int c = 0;
    for (int i = 0; i < n; ++i) if (storage_id(save, kind, i) >= lo) ++c;
    return c;
}

ShopResult storage_store(UserStatus& s, SaveData& save, const ItemDb& db, const ItemRef& ref) {
    ShopResult why = ShopResult::Ok;
    if (!item_tradeable(s, db, ref, &why)) return why;
    const int kind = ref.kind;
    const int n = kind == 0 ? kStorageItems : kind == 1 ? kStorageWeapons : kStorageAttach;
    const int lo = kind == 0 ? 0x84 : kind == 1 ? 0x101 : 0x51;
    int free_slot = -1;
    for (int i = 0; i < n; ++i) if (storage_id(save, kind, i) < lo) { free_slot = i; break; }
    if (free_slot < 0) return ShopResult::StorageFull;
    uint8_t* b = storage(save);
    switch (kind) {
        case 0:
            std::memcpy(b + size_t(free_slot) * 2, s.raw.data() + bag_at(ref.slot), 2);
            std::memcpy(b + 0x78 + size_t(free_slot) * 2, s.raw.data() + 0x443c + size_t(ref.slot) * 2, 2);
            s.set<int16_t>(bag_at(ref.slot), -1);
            s.set<int16_t>(0x443c + size_t(ref.slot) * 2, 0);
            break;
        case 1:
            std::memcpy(b + 0xf0 + size_t(free_slot) * 0xf8, s.raw.data() + weapon_at(ref.owner, ref.slot), 0xf8);
            std::memset(s.raw.data() + weapon_at(ref.owner, ref.slot), 0, 0xf8);
            s.set<int16_t>(weapon_at(ref.owner, ref.slot), -1);
            break;
        default:
            std::memcpy(b + 0x1e00 + size_t(free_slot) * 0x20, s.raw.data() + attach_at(ref.slot), 0x20);
            std::memset(s.raw.data() + attach_at(ref.slot), 0, 0x20);
            s.set<int16_t>(attach_at(ref.slot), -1);
            break;
    }
    return ShopResult::Ok;
}

ShopResult storage_take(UserStatus& s, SaveData& save, const ItemDb& db, int kind, int slot, GameRand& rng) {
    (void)rng;
    const int id = storage_id(save, kind, slot);
    const int lo = kind == 0 ? 0x84 : kind == 1 ? 0x101 : 0x51;
    if (id < lo) return ShopResult::Nothing;
    uint8_t* b = storage(save);
    if (kind == 0) {
        if (check_item_get(s, id) != 0) return ShopResult::NoSpace;
        for (int i = 0; i < kBagSlots; ++i)
            if (s.get<int16_t>(bag_at(i)) < 0x84) {
                s.set<int16_t>(bag_at(i), int16_t(id));
                s.set<int16_t>(0x443c + size_t(i) * 2, int16_t(storage_value(save, 0, slot)));
                b[size_t(slot) * 2] = 0xff; b[size_t(slot) * 2 + 1] = 0xff;
                std::memset(b + 0x78 + size_t(slot) * 2, 0, 2);
                return ShopResult::Ok;
            }
        return ShopResult::NoSpace;
    }
    if (kind == 1) {
        const int owner = weapon_owner(db, id);
        if (owner < 0 || owner > 5 || owned_weapons(s, owner) >= 10) return ShopResult::NoSpace;
        for (int i = 0; i < 11; ++i)
            if (s.get<int16_t>(weapon_at(owner, i)) < 0x101) {
                std::memcpy(s.raw.data() + weapon_at(owner, i), b + 0xf0 + size_t(slot) * 0xf8, 0xf8);
                std::memset(b + 0xf0 + size_t(slot) * 0xf8, 0, 0xf8);
                b[0xf0 + size_t(slot) * 0xf8] = 0xff; b[0xf0 + size_t(slot) * 0xf8 + 1] = 0xff;
                return ShopResult::Ok;
            }
        return ShopResult::NoSpace;
    }
    if (owned_attach(s) >= 0x28) return ShopResult::NoSpace;
    for (int i = 0; i < 0x2b; ++i)
        if (s.get<int16_t>(attach_at(i)) < 0x51) {
            std::memcpy(s.raw.data() + attach_at(i), b + 0x1e00 + size_t(slot) * 0x20, 0x20);
            std::memset(b + 0x1e00 + size_t(slot) * 0x20, 0, 0x20);
            b[0x1e00 + size_t(slot) * 0x20] = 0xff; b[0x1e00 + size_t(slot) * 0x20 + 1] = 0xff;
            return ShopResult::Ok;
        }
    return ShopResult::NoSpace;
}

}  // namespace dccore
