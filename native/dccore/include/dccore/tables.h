#pragma once
// Game data tables read from the main executable (see elf.h): the monster database, which monsters live on which floor, and the areas where items and
// monsters may be put in each dungeon piece.
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "dccore/elf.h"
#include "dccore/shots.h"

namespace dccore {

// One row of MonstorTable (0x9c bytes). Field meanings are decoded as the code that uses them is ported.
struct MonsterEntry {
    std::array<uint8_t, 0x9c> raw{};
    std::string stem() const;               // +0x00: file stem of the model pack (dun/monstor/<stem>.chr)
    std::string extra_stem(int i) const;    // +0x10, +0x20, +0x30: optional additional model packs (i = 0..2), empty if unused
    std::string script_stem() const;        // +0x40: script (.stb) stem
    int16_t s16(size_t off) const;
    int32_t s32(size_t off) const;
    float f32(size_t off) const;
    int kind() const { return s16(0x78); }  // 0/3 may repeat on a floor; other kinds are placed once per arrangement
};

struct LayoutSlot {
    int32_t kind = 0, monster = -1, weight = 0;   // monster index into the database, -1 = end of the list
};

// Entries of the item/monster placement areas of one dungeon piece (ItemFreeAreaAll): up to four boxes in the piece's own coordinates (divided by 10).
struct FreeAreaEntry {
    int8_t part = -1, count = 0, rot = 0;
    float box[4][6] = {};   // per box (in tenths of units): x1, y1, z1, x2, y2, z2 (stored at +4..+0x18 of the 0x18-byte record)
};

class GameTables {
public:
    static constexpr int kMaps = 7;
    std::vector<MonsterEntry> monsters;
    // [back floor ? 1 : 0][map][floor] -> 9 slots
    std::vector<std::array<LayoutSlot, 9>> layout[2][kMaps];
    std::vector<FreeAreaEntry> free_areas[kMaps];
    std::vector<ShotDef> shot_defs;   // BtEntryEffectTbl: the shot effects monsters can use

    static GameTables load(const Elf& elf);
};

}  // namespace dccore
