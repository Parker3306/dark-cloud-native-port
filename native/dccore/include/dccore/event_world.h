#pragma once
// What the player can do with the floor's events: open chests, take atra statues, use the gate key / door keys, walk into the exit.
// Replaces the parts of MoveChara's event handling (gameTask 0x78 / 0x82 / 0x8c, the CDungeonEventMan areas) that matter for playing; the chest-opening
// cut-scenes, message windows and the scripted stair walks of the original are presentation and left to the front end.
#include <string>
#include <vector>
#include "dccore/events.h"
#include "dccore/floor.h"
#include "dccore/items.h"

namespace dccore {

struct Notice {
    enum Kind {
        ItemGot,        // a = item id, b = amount (attach materials)
        WeaponGot,      // a = weapon id
        MapGot,         // the dungeon map
        CrystalGot,     // the magic crystal
        BagFull,        // a = item id left in the chest
        AttachFull,
        WeaponFull,
        AtraGot,        // a = atra id (index of the floor's atra list)
        NeedGateKey,
        GateOpened,     // a = cell x, b = cell y of the gate
        NeedDoorKey,
        DoorOpened,     // a = cell x, b = cell y of the locked door
        ExitFloor,      // the player walked into the exit stairs
        OnlyToan,       // atra statues can only be taken by Toan
        Trap,           // a = effect: 0 poison, 1 gold bonus, 5 monsters appear, 6 gold loss, 2/3/4/7/8/9 change the weapon (the first weapon of a character is never changed: poison)
        Mimic,          // a = monster slot: the chest was a monster; it jumps out (its revive script runs)
        TrapChest,      // a = chest index, b = trap id 1..5: the chest is trapped, the dungeon script asks what to do (function 15; 16 for the Happy Clown, id 5)
    } kind;
    int a = 0, b = 0;
};

struct MarkerState {
    bool on = true;           // the trigger can fire (PT_MARKER switch)
    bool inside = false;      // the player stood in it last tick
    bool item_use = false;    // the item-use button works here (slot +0x30): cleared by _HOLD_ITEM_EVENT once the key item did its job
};

class EventWorld {
public:
    FloorEvents ev;
    std::vector<FloorMarker> markers;
    std::vector<MarkerState> marker_state;
    int map_no = 0, floor_no = 0;
    int gate_key_item = 195;
    bool have_map = false, have_crystal = false;
    std::vector<int> atra_collected;      // atra ids taken on this floor
    std::vector<Notice> notices;          // events for the front end, taken with take_notices()
    int active_event = -1;                // event slot the player stands in (GetActiveIvent), -1 for none
    int exit_delay = 0;                   // ticks until the exit switches on after the gate was opened (the gate swings open first)
    FloorLayout* layout = nullptr;        // opened doors are recorded in layout->door_open (collision and drawing follow)

    void init(FloorEvents events, std::vector<FloorMarker> floor_markers, FloorLayout* floor_layout, int map, int floor, int gate_key);
    // One tick. `ok_down`: the action button went down; `free`: the player is not in the middle of an attack or hurt.
    void step(const float player_pos[3], bool ok_down, bool free, int character, UserStatus& status, const ItemDb& items, GameRand& rng);
    std::vector<Notice> take_notices() { std::vector<Notice> n; n.swap(notices); return n; }
    int marker_index_at(const float pos[3]) const;   // first usable marker the position stands in, -1 for none
};

// Bag helpers (the ordinary item slots, ids 0x84..0x100).
bool has_item(const UserStatus& s, int id);
bool remove_item(UserStatus& s, int id);
int weapon_count(const UserStatus& s, const ItemDb& items, int weapon_id);          // CheckWeaponRot: weapons the owner of this weapon already holds
int create_attach_volume(int item, GameRand& rng);                                   // createAttachVolume: how many pieces of an attach material a chest holds

// The item that opens the exit gate of a dungeon (UserStatus floor start: buildRandomMap stores it at map + 0x464); `variant` is FloorLayout::map1_variant.
int gate_key_for(int map_no, int variant);

}  // namespace dccore
