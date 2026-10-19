#include "dccore/tables.h"

namespace dccore {

namespace {
constexpr uint32_t kMonsterTable = 0x27FB00, kMonsterCount = 26052 / 0x9c;
constexpr uint32_t kLayoutList = 0x2917B0, kBackLayoutList = 0x2917D0, kFreeAreaList = 0x279D50;
constexpr int kFloorsPerMap[GameTables::kMaps] = {15, 17, 18, 18, 15, 26, 100};   // sizes of BtEnemyLayout00..06 / 0x70
std::string fixed(View v) {
    std::string s;
    for (uint8_t c : v) {
        if (!c) break;
        s += char(c);
    }
    return s;
}
}  // namespace

std::string MonsterEntry::stem() const { return fixed(View(raw.data(), 16)); }
std::string MonsterEntry::extra_stem(int i) const { return fixed(View(raw.data() + 0x10 + 0x10 * size_t(i), 16)); }
std::string MonsterEntry::script_stem() const { return fixed(View(raw.data() + 0x40, 16)); }
int16_t MonsterEntry::s16(size_t off) const { int16_t v; std::memcpy(&v, raw.data() + off, 2); return v; }
int32_t MonsterEntry::s32(size_t off) const { int32_t v; std::memcpy(&v, raw.data() + off, 4); return v; }
float MonsterEntry::f32(size_t off) const { float v; std::memcpy(&v, raw.data() + off, 4); return v; }

GameTables GameTables::load(const Elf& elf) {
    GameTables t;
    for (uint32_t i = 0; i < kMonsterCount; ++i) {
        MonsterEntry e;
        std::memcpy(e.raw.data(), elf.at(kMonsterTable + i * 0x9c, 0x9c).data(), 0x9c);
        t.monsters.push_back(e);
    }
    for (int back = 0; back < 2; ++back)
        for (int m = 0; m < kMaps; ++m) {
            const uint32_t base = elf.get<uint32_t>((back ? kBackLayoutList : kLayoutList) + 4u * uint32_t(m));
            for (int f = 0; f < kFloorsPerMap[m]; ++f) {
                std::array<LayoutSlot, 9> slots;
                for (int s = 0; s < 9; ++s) {
                    const uint32_t a = base + uint32_t(f) * 0x70 + uint32_t(s) * 12;
                    slots[size_t(s)] = {elf.get<int32_t>(a), elf.get<int32_t>(a + 4), elf.get<int32_t>(a + 8)};
                }
                t.layout[back][m].push_back(slots);
            }
        }
    for (int m = 0; m < kMaps; ++m) {
        const uint32_t base = elf.get<uint32_t>(kFreeAreaList + 4u * uint32_t(m));
        for (uint32_t e = 0;; ++e) {
            const uint32_t a = base + e * 100;
            const int8_t part = elf.get<int8_t>(a);
            if (part == -1 || e > 400) break;
            FreeAreaEntry fe;
            fe.part = part;
            fe.count = elf.get<int8_t>(a + 1);
            fe.rot = elf.get<int8_t>(a + 2);
            for (int q = 0; q < 4; ++q)
                for (int k = 0; k < 6; ++k) fe.box[q][k] = elf.get<float>(a + 4 + uint32_t(q) * 0x18 + uint32_t(k) * 4);
            t.free_areas[m].push_back(fe);
        }
    }
    t.shot_defs = ShotSystem::load_table(elf);
    return t;
}

}  // namespace dccore
