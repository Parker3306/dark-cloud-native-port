#include "dccore/items.h"
#include <algorithm>
#include <cstring>

namespace dccore {

namespace {
constexpr uint32_t kComItemInfo = 0x27DE50, kComItemInfoSize = 2368;
constexpr uint32_t kItemList = 0x27D0A0, kItemListSize = 3500, kItemStride = 0x14;
constexpr uint32_t kAttachList = 0x27CA60, kAttachListSize = 1600, kAttachStride = 0x20;
constexpr uint32_t kWeaponList = 0x27A6C0, kWeaponListSize = 9120, kWeaponStride = 0x4c;
constexpr int kDefaultWeapons[6] = {257, 299, 314, 331, 347, 363};

int16_t s16(const uint8_t* p, size_t off) { int16_t v; std::memcpy(&v, p + off, 2); return v; }
void put16(uint8_t* p, size_t off, int16_t v) { std::memcpy(p + off, &v, 2); }
}  // namespace

ItemDb ItemDb::load(const Elf& elf) {
    ItemDb db;
    auto copy = [&](std::vector<uint8_t>& out, uint32_t addr, uint32_t n) {
        const View v = elf.at(addr, n);
        out.assign(v.begin(), v.end());
    };
    copy(db.info_, kComItemInfo, kComItemInfoSize);
    copy(db.items_, kItemList, kItemListSize);
    copy(db.attach_, kAttachList, kAttachListSize);
    copy(db.weapons_, kWeaponList, kWeaponListSize);
    return db;
}

ItemDb::Info ItemDb::info(int id) const {
    if (id < 1) return {};
    const int idx = (id <= 0x50) ? id + 0xaf : id - 0x51;
    if (size_t(idx) * 8 + 8 > info_.size()) return {};
    return {int(info_[size_t(idx) * 8]), int(s16(info_.data(), size_t(idx) * 8 + 2))};
}

int ItemDb::icon_cell(int id) const {
    const int idx = id <= 0x50 ? id + 0xaf : id - 0x51;
    if (idx < 0 || size_t(idx + 1) * 8 > info_.size()) return -1;
    int16_t v;
    std::memcpy(&v, info_.data() + size_t(idx) * 8 + 4, 2);
    return v;
}

int ItemDb::name_message(int id) const {
    if (id < 1) return id + 100;
    const int idx = (id <= 0x50) ? id + 0xaf : id - 0x51;
    if (size_t(idx) * 8 + 8 > info_.size()) return id + 100;
    return int(s16(info_.data(), size_t(idx) * 8 + 6)) + 100;
}

const uint8_t* ItemDb::item(int id) const {
    const Info i = info(id);
    if (i.type != 1 || i.index < 0 || size_t(i.index + 1) * kItemStride > items_.size()) return nullptr;
    return items_.data() + size_t(i.index) * kItemStride;
}

const uint8_t* ItemDb::attach(int id) const {
    const Info i = info(id);
    if (i.type != 0 || i.index < 0 || size_t(i.index + 1) * kAttachStride > attach_.size()) return nullptr;
    return attach_.data() + size_t(i.index) * kAttachStride;
}

const uint8_t* ItemDb::weapon(int id) const {
    const Info i = info(id);
    if (i.type != 2 || i.index < 0 || size_t(i.index + 1) * kWeaponStride > weapons_.size()) return nullptr;
    return weapons_.data() + size_t(i.index) * kWeaponStride;
}

int ItemDb::weapon_max_exp(const uint8_t* rec) const {
    if (!rec) return 0;
    const uint8_t* w = weapon(s16(rec, 0));
    if (!w) return 1;
    int v = int(int8_t(w[0x30]));
    for (int l = 0; l < s16(rec, 2); ++l) v += s16(w, 0x32);
    if (999 < v) v = 999;
    if (v < 1) v = 99;
    return v;
}

int ItemDb::item_value(int id, GameRand& rng) const {
    if (id < 0x84 || 0x100 < id) return 0;
    const uint8_t* e = item(id);
    if (!e) return -1;
    int v = s16(e, 10);
    if (id == 0xeb) {
        int r = rng.next() % 0x168;
        if (r < 0xb4) r += 0xb4;
        v = r * 0x3c;
    }
    if (0x83 < id && id < 0x88) v = rng.next() % 5 + 3;
    return v;
}

void ItemDb::make_weapon(int id, uint8_t* rec, GameRand& rng) const {
    if (id < 1) return;
    if (id < 0x51) id += 0x100;
    const uint8_t* e = weapon(id);
    if (!e) return;
    std::memset(rec, 0, 0xf8);
    put16(rec, 0, int16_t(id));
    put16(rec, 4, s16(e, 2));
    put16(rec, 6, s16(e, 4));
    put16(rec, 8, s16(e, 6));
    put16(rec, 10, s16(e, 8));
    put16(rec, 12, s16(e, 0));
    const float dur = float(s16(rec, 12));
    std::memcpy(rec + 0x10, &dur, 4);
    int best = 5;
    int8_t best_value = 0;
    for (int i = 0; i < 5; ++i) {
        rec[0x17 + i] = e[size_t(9 + i) * 2];
        const int8_t v = int8_t(rec[0x17 + i]);
        if (best_value < v) { best_value = v; best = i; }
    }
    rec[0x16] = uint8_t(best);
    for (int i = 0; i < 10; ++i) rec[0x1c + i] = e[size_t(14 + i) * 2];
    put16(rec, 0xee, s16(e, 0x38));
    const bool is_default = std::find(std::begin(kDefaultWeapons), std::end(kDefaultWeapons), id) != std::end(kDefaultWeapons);
    if (!is_default) {   // a few weapons come with a bonus or a curse
        const int r = rng.next() % 1000;
        uint16_t flags = uint16_t(s16(rec, 0xee));
        if (r < 10) flags |= 0x200;
        if (0x3dd < r) flags |= 0x100;
        put16(rec, 0xee, int16_t(flags));
    }
}

int check_item_get(const UserStatus& s, int id) {
    if (uint32_t(id - 0xe9) < 2 || id == 0xee) return 0;
    if (0x83 < id && id < 0x101) {
        int count = 0;
        for (int i = 0; i < 100; ++i) {
            const int16_t v = s.get<int16_t>(0x436e + size_t(i) * 2);
            if (0x83 < v && v < 0x101) ++count;
        }
        for (int i = 0; i < 3; ++i)
            if (s.get<int16_t>(0x4362 + size_t(i) * 2) != -1) count += s.get<int16_t>(0x4368 + size_t(i) * 2);
        if (int(s.get<int8_t>(0x4360)) <= count) return 1;
    }
    if (0x50 < id && id < 0x84) {
        int count = 0;
        for (int i = 0; i < 0x28; ++i) {
            const int16_t v = s.get<int16_t>(0x84fc + size_t(i) * 0x20);
            if (0x50 < v && v < 0x84) ++count;
        }
        return count < 0x28 ? 0 : 2;
    }
    return 0;
}

int get_item(UserStatus& s, const ItemDb& db, int id, GameRand& rng, int attach_value) {
    if (0x83 < id && id < 0x101) {
        for (int slot = 0; slot < kBagSlots; ++slot) {
            const size_t at = 0x436e + size_t(slot) * 2;
            if (s.get<int16_t>(at) >= 0x84) continue;
            if (id == 0xee) {
                s.set<int32_t>(0x4320, 1);
                return 0;
            }
            int count = 0;
            for (int i = 0; i < kBagSlots; ++i)
                if (0x83 < s.get<int16_t>(0x436e + size_t(i) * 2)) ++count;
            for (int i = 0; i < 3; ++i)
                if (s.get<int16_t>(0x4362 + size_t(i) * 2) != -1) count += s.get<int16_t>(0x4368 + size_t(i) * 2);
            if (int(s.get<int8_t>(0x4360)) < count + 1) s.set<int32_t>(0x431c, 1);
            const int16_t value = int16_t(db.item_value(id, rng));
            if (s.get<int32_t>(0x431c) != 0) {   // the bag is full: the item goes into one of the three spill-over slots behind it
                for (int k = 0; k < 3; ++k) {
                    const size_t o = (size_t(int(s.get<int8_t>(0x4360))) + size_t(k)) * 2;
                    if (s.get<int16_t>(0x436e + o) == -1) {
                        s.set<int16_t>(0x436e + o, int16_t(id));
                        s.set<int16_t>(0x443c + o, value);
                        return slot;
                    }
                }
                return slot;
            }
            s.set<int16_t>(at, int16_t(id));
            s.set<int16_t>(0x443c + size_t(slot) * 2, value);
            return slot;
        }
    }
    if (0x50 < id && id < 0x84) {
        for (int slot = 0; slot < 0x2b; ++slot) {
            const size_t at = 0x84fc + size_t(slot) * 0x20;
            if (s.get<int16_t>(at) >= 0x51) continue;
            s.set<int16_t>(at, int16_t(id));
            if (const uint8_t* e = db.attach(id)) {   // SetAttachMentValue: the record is copied from the table
                std::memset(s.raw.data() + at, 0, 0x20);
                std::memcpy(s.raw.data() + at, e, 0x20);
                int v = attach_value < 1 ? 1 : attach_value;
                if (3 < v) v = 3;
                const int16_t sid = s.get<int16_t>(at);
                if (0x5a < sid && sid < 0x5f) s.set<int16_t>(at + size_t(sid - 0x57) * 2, int16_t(s.get<int16_t>(at + size_t(sid - 0x57) * 2) + v));
            }
            int count = 0;
            for (int i = 0; i < 0x2b; ++i) {
                const int16_t v = s.get<int16_t>(0x84fc + size_t(i) * 0x20);
                if (0x50 < v && v < 0x84) ++count;
            }
            if (0x29 <= count) s.set<int32_t>(0x431c, s.get<int32_t>(0x431c) + 1);
            return slot;
        }
    }
    if (0x100 < id) {
        const uint8_t* w = db.weapon(id);
        if (!w) return -1;
        const int owner = int(int8_t(w[10]));
        for (int slot = 0; slot < 0xb; ++slot) {
            const size_t at = 0x450c + size_t(owner) * 0xaa8 + size_t(slot) * 0xf8;
            if (s.get<int16_t>(at) >= 0x101) continue;
            db.make_weapon(id, s.raw.data() + at, rng);
            int count = 0;
            for (int i = 0; i < 10; ++i)
                if (0x100 < s.get<int16_t>(0x450c + size_t(owner) * 0xaa8 + size_t(i) * 0xf8)) ++count;
            if (count < 10) return slot;
            s.set<int32_t>(0x431c, 1);
            return slot;
        }
    }
    return -1;
}

}  // namespace dccore
