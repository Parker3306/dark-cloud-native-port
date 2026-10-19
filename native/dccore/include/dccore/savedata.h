#pragma once
// CSaveData: everything the game remembers between visits (0x131c0 bytes, the layout the game keeps in memory and writes to the memory card): the player's
// options and names, the clock and day, fishing records, the Georama (town) state of every town, the player record of the dungeons (CDngStatusData, which is our
// UserStatus), the storage of the town (CStockItem) and the story / map flags. Ported from the decompiled CSaveData methods (0x157b30..0x158d30).
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>
#include "dccore/user_status.h"

namespace dccore {

class SaveData {
public:
    static constexpr size_t kSize = 0x131c0;
    // offsets of the blocks
    static constexpr size_t kConfig = 0x0, kCharaName = 0x48, kNowTime = 0x1d0, kPlayTime = 0x1d4, kDay = 0x1d8, kFishingPoint = 0x1dc, kMenuCursor = 0x650;
    static constexpr size_t kGrd = 0x67c, kGrdSize = 3000;             // per town: edit parts info (24 x 0x18 at +0x68), the parts list (0x82 x 0x10 at +0x2a8), npc data (20 x 0xc at +0xac8)
    static constexpr size_t kElem = 0x4ccc;                            // 6 x 0x100 (0x80 shorts each, 0xffff = none)
    static constexpr size_t kSubMapNpc = 0x52cc;
    static constexpr size_t kDngStatus = 0x540c;                       // the UserStatus (CDngStatusData)
    static constexpr size_t kStatusSize = 0xe0a8 - 0x540c;
    static constexpr size_t kStockItem = 0xe0a8;                       // CStockItem: the storage of the town
    static constexpr size_t kGameFlags = 0x10268;                      // 256 words of bits
    static constexpr size_t kGameInts = 0x10668;                       // 32 ints
    static constexpr size_t kMapFlags = 0x106e8;                       // 80 maps x 16 words
    static constexpr size_t kMapInitFlags = 0x11ae8;                   // 80 maps x 16 words
    static constexpr size_t kVisits = 0x12ee8;                         // 80 shorts

    std::array<uint8_t, kSize> raw{};

    SaveData() { initialize(); }
    void initialize();   // Initialize: zero, then the non-zero defaults (-1 markers in the parts lists, the empty player record)

    template <class T>
    T get(size_t off) const { T v; std::memcpy(&v, raw.data() + off, sizeof(T)); return v; }
    template <class T>
    void set(size_t off, T v) { std::memcpy(raw.data() + off, &v, sizeof(T)); }

    // the characters' names (6 x 0x20 glyph codes; the alphabet sheet cell + 0xa2, 0xe6 = space, 0 = empty = the default name)
    std::string hero_name(int ch) const;
    void set_hero_name(int ch, const std::string& name);
    // story flags
    bool game_flag(int i) const;
    void set_game_flag(int i, bool on);
    int32_t game_int(int i) const { return i < 0x20 ? get<int32_t>(kGameInts + size_t(i) * 4) : 0; }
    void set_game_int(int i, int32_t v) { if (i >= 0 && i < 0x20) set<int32_t>(kGameInts + size_t(i) * 4, v); }
    // per map: visible flags and "already set up" flags (bit n of word n >> 5 of the map's 16 words)
    bool map_flag(int map, int bit) const { return bit_of(kMapFlags, map, bit); }
    void set_map_flag(int map, int bit, bool on) { set_bit_of(kMapFlags, map, bit, on); }
    bool map_init_flag(int map, int bit) const { return bit_of(kMapInitFlags, map, bit); }
    void set_map_init_flag(int map, int bit, bool on) { set_bit_of(kMapInitFlags, map, bit, on); }
    int visit_map(int map, int times);   // VisitMap: how often the player was in a map (capped at 9999); returns the count
    // the clock: now_time counts hours 0..12 (the day advances when it passes 8.5), day 0..9998
    float now_time() const { return get<float>(kNowTime); }
    void set_now_time(float t) { set<float>(kNowTime, t); }
    void add_now_time(float dt);
    int day() const { return get<int32_t>(kDay); }
    void set_day(int d);
    void add_day(int d) { set_day(day() + d); }
    int32_t play_time() const { return get<int32_t>(kPlayTime); }
    void add_play_time(int ticks) { set<int32_t>(kPlayTime, play_time() + ticks); }
    int fishing_points() const { return get<int32_t>(kFishingPoint); }
    void add_fishing_points(int d);
    // the character names (6 x 0x40 bytes)
    uint8_t* chara_name(int ch) { return ch < 0 || ch > 5 ? nullptr : raw.data() + kCharaName + size_t(ch) * 0x40; }
    // town data blocks (towns 0..5)
    const uint8_t* grd_data(int town) const { return town < 0 || town > 5 ? nullptr : raw.data() + kGrd + size_t(town) * kGrdSize; }
    uint8_t* grd_data(int town) { return town < 0 || town > 5 ? nullptr : raw.data() + kGrd + size_t(town) * kGrdSize; }
    uint8_t* edit_parts_info(int town, int index) { return town < 0 || town > 5 || index < 0 || index > 0x17 ? nullptr : raw.data() + kGrd + size_t(town) * kGrdSize + 0x68 + size_t(index) * 0x18; }
    uint8_t* elem_data(int ch) { return ch < 0 || ch > 5 ? nullptr : raw.data() + kElem + size_t(ch) * 0x100; }
    int parts_count(int town) const;   // GetParts: how many entries of the town's parts list are in use
    // The parts the player placed in a town (CEditGround::Save(map, CSaveData*)): up to 0x82 records of {i16 part, i16 rot, f32 x, y, z} ending at part < 0.
    struct GrdPart {
        int16_t part = -1, rot = 0;
        float pos[3] = {0, 0, 0};
    };
    std::vector<GrdPart> town_parts(int town) const;
    void set_town_parts(int town, const std::vector<GrdPart>& parts);

    // the player record lives inside the save data
    void store_status(const UserStatus& s) { std::memcpy(raw.data() + kDngStatus, s.raw.data(), kStatusSize); }
    void load_status(UserStatus& s) const { std::memcpy(s.raw.data(), raw.data() + kDngStatus, kStatusSize); }

private:
    bool bit_of(size_t base, int map, int bit) const;
    void set_bit_of(size_t base, int map, int bit, bool on);
};

}  // namespace dccore
