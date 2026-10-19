#pragma once
// Things a dungeon floor holds besides monsters: treasure chests, atra statues ("atla balls"), trap circles, and the slots that make them triggerable events.
// Port of CDungeonMap::buildEventData / SetTreasureBox / SetAtraBoll / SetupTrapCircle / CheckTreasureBox / CheckAtra / CheckTrapCircle / GetActiveIvent,
// PresetSmallItemNo_Get, BtAtraListMake and BtAtraFloorCyoice.
//
// Known difference from the real game: PresetSmallItemNo_Get compares a random roll with a rate table that the game copies from the wrong address (it copies 380 bytes
// from `ItemSetRateTbl + map * 4`, i.e. the pointer table itself, instead of from the table it points to). Rates of items 1..190 therefore come out of whatever data
// follows that address (reproduced here from the disc); rates of the other items come from uninitialised stack memory, which is treated as zero here.
#include <array>
#include <cstdint>
#include <vector>
#include "dccore/dungeon.h"
#include "dccore/elf.h"
#include "dccore/placement.h"
#include "dccore/tables.h"
#include "dccore/user_status.h"

namespace dccore {

struct PutListRecord {
    int key = -1;                 // floor + 1, 0x100 (floors below the table's floor count) or 0xff (the rest)
    std::vector<int> ids;         // item ids that may be put in chests on such a floor
};

class EventTables {
public:
    static EventTables load(const Elf& elf);
    std::vector<PutListRecord> put_lists[14];    // ItemPutListPtr: [map] normal floors, [map + 7] the other table
    int floor_num[7] = {};
    std::vector<std::array<int, 3>> atra_appear[6];   // AtraAppearData: {atra id, floor (-1 early, -2 late, else floor + 1), count}
    std::vector<int> no_entry[6];                // floors (1-based) that never hold atra
    int center_floor[6] = {};
    int max_floor[7] = {};
    std::array<uint8_t, 380> rate_bytes[7] = {};
    int rate(int map, int id) const;             // see the note above

    // PresetSmallItemNo_Get(map, floor, table, small): item id for a chest or -1
    int preset_item(int map, int floor, int table, bool small, GameRand& rng) const;

    // PieroItemListPtr[map + 7 * back]: 0x104 words, shallow floors (floor < floor_num) use the first two lists {count A, count B, A[0x40], B[0x40]}, the deep floors the second two ({0x82..}).
    std::array<int32_t, 0x104> piero[14] = {};
    // GetPieroItem (_GET_PIERO_ITEM): the two items the Happy Clown offers (the player takes one): out[0] from list A, out[1] from list B.
    void piero_item(int map, int back, int floor, GameRand& rng, int out[2]) const;
};

struct FloorEvent {
    int type = -1;                // -1 free, 2 chest, 3 atra statue, 8 mimic
    int state = 0;
    float pos[4] = {0, 0, 0, 1};
    float radius = 0;
    int index = 0;                // chest or atra number
};

struct Chest {
    bool active = false;
    float pos[4] = {0, 0, 0, 1};
    int item = 0;
    int small = 1;                // 1 small chest (one item), 0 big chest (weapon)
    int state = 0;                // 0 closed
    int quality = 0;              // big chests: the trap id 0..5 (_GET_ITEM_TRAPID; 0 = safe, 5 = the Happy Clown): a trapped chest runs the dungeon script (function 15, 16 for 5) instead of opening
};

struct AtraBall {
    float pos[4] = {0, 0, 0, 1};
    int id = 0;                   // index into AtraAppearData[map]
    bool active = false;
    int state = 0;
};

struct TrapCircle {
    float pos[4] = {0, 0, 0, 1};
    int state = 0;                // 0 free, 1 armed, 2 triggered
    int kind = 0;                 // 0..9
    float timer = 0;
};

struct FloorEvents {
    std::array<FloorEvent, 0x30> events;
    std::array<Chest, 0x18> chests;
    int chest_count = 0;
    std::array<AtraBall, 8> atras;
    int atra_count = 0;
    std::array<TrapCircle, 3> traps;

    int free_event() const;
    int set_chest(const float pos[4], int item, bool small, int quality);      // SetTreasureBox
    int set_mimic(const float pos[4], bool small, int monster_slot);           // SetMimicEvent: a chest that holds a monster (event type 8)
    void set_atra(const float pos[4], int atra_id);                            // SetAtraBoll
    bool chest_near(const float pos[4], float dist) const;                     // CheckTreasureBox == 0
    bool atra_near(int map, const float pos[4], float dist) const;             // CheckAtra == 0
    bool trap_near(const float pos[4], float dist) const;                      // CheckTrapCircle != 0
    int active_event(const float player[4]) const;                             // GetActiveIvent: first event the player stands in, -1 for none
};

// UserStatus helpers for the atra lists (+0x278 per floor list, +0x2078 the dungeon's table, +0x42bf + map = 0xff until the table was made).
int atra_list_count(const UserStatus& s, int map, int floor);
void atra_list_make(UserStatus& s, const EventTables& t, int map, GameRand& rng);   // BtAtraListMake
int atra_floor_choice(const UserStatus& s, const EventTables& t, int map, int floor, int out[8], GameRand& rng);   // BtAtraFloorCyoice

// buildEventData(map, floor, build = 1, first): `ev` must already hold the chest the floor generator put down (FloorLayout::boxes, see make_floor_events).
void build_event_data(FloorEvents& ev, const FloorLayout& layout, const GameTables& tables, const EventTables& et, const UserStatus& status, int map, int floor, bool main_floor, GameRand& rng);

// The chest(s) of the generator turned into the first events of a floor (setRoomObject: item 0xd8 + map, small chest).
FloorEvents make_floor_events(const FloorLayout& layout, int map);

}  // namespace dccore
