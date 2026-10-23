#pragma once
// Random dungeon floor layout: a faithful port of CDungeonMap::buildRandomMap and the functions it calls (buildRoom, joinRoom, setRoomObject,
// setRoomDivid, setUnderDungeonStart, setStair, mapPartsFilter). The game uses the C library's rand() (a 32-bit LCG), so a floor is a pure function
// of its seed. Cell and part numbering are the game's own; see notes/native-plan.md, Milestone 3.
#include <array>
#include <map>
#include <cstdint>
#include <string>
#include <vector>

namespace dccore {

constexpr int kGrid = 20;  // the floor is a 20 x 20 grid of cells (rooms stay inside the 16 x 16 top-left area)

struct MapCell {
    int32_t part = -1;                 // part number (index of a DEF_PATS entry in the dungeon's .cfg), -1 = empty
    int32_t rot = 0;                   // rotation in quarter turns (stairs/doors: facing)
    std::array<int32_t, 16> room{};    // room membership flags (room index -> 1)
    uint32_t flags = 0;                // 1 room, 2 corridor, 4 door, 8 object marker, 0x10 divider wall, 0x20 divider door,
                                       // 0x40 decoration, 0x80 stairs up, 0x100 stairs down, 0x200 start, 0x400 link door
};

struct RoomRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct FloorLayout {
    std::array<MapCell, kGrid * kGrid> cell;
    std::vector<RoomRect> rooms;
    int map_no = 0;
    uint32_t seed = 0;
    // The generator's rand() state after the layout was built (the game keeps drawing from it for monsters and items).
    uint32_t rand_state = 0;
    std::vector<uint8_t> door_open;    // per cell (kGrid * kGrid): bit mask of the door kinds opened there (1 locked door, 2 exit gate; see door_kind_of); empty = none
    std::map<std::string, std::array<float, 3>> frame_moves;   // fixed maps: frames a room script moved (boss gates, bridges): their collision follows (_SET_OBJHDL_POS offsets)
    // Dran's arena floor (d01boss, CDranMapField): per tile a counter, 3 = whole; a shot that hits the tile's top takes it down by one while it is above 1 (checkCollision); at 2 the tile's Step plays its sound
    // and sets 1 = crumbling (no collision, the model plays its motion until frame 59), then 0 = gone. Mutable: the monster side only holds a const layout.
    mutable std::array<int, 12> dran_state = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    mutable std::array<float, 12> dran_time = {};
    mutable std::vector<int> dran_events;   // tiles that started to crumble since the front end looked (the sound)
    bool fixed = false;                // a fixed map (story rooms, boss rooms): every part of the cfg is placed once at the origin, there is no grid
    int map1_variant = 0;              // map 1 only: 0..2, picks the floor's tint (buildRandomMap writes one of three constants to +0x464)
    // Treasure boxes put on the floor so far (world positions). Only filled when the generator was given the game tables (GenState::tables).
    std::vector<std::array<float, 3>> boxes;
    // d05 (map 4) only: the "dummy models" buildDummyModel places on empty ground (up to 8: world position and kind 0..2 = footprint 2x2 / 2x2 / 3x3 cells); the game draws them as scenery.
    std::vector<std::array<float, 3>> dummy_pos;
    std::vector<int> dummy_kind;

    MapCell& at(int x, int y) { return cell[size_t(y) * kGrid + size_t(x)]; }
    const MapCell& at(int x, int y) const { return cell[size_t(y) * kGrid + size_t(x)]; }
};

// The C library rand() of the game: state = state * 0x41C64E6D + 0x3039; returns state & 0x7FFFFFFF.
struct GameRand {
    uint32_t state = 1;
    uint64_t draws = 0;   // how many numbers were taken (for comparing with the game)
    void seed(uint32_t s) { state = s; }
    int32_t next() {
        ++draws;
        state = state * 0x41C64E6Du + 0x3039u;
        return int32_t(state & 0x7FFFFFFFu);
    }
};

// Memory the game leaves behind between floors and later reads by accident: GetRoomLinkInfo's 16x16 scan array sits directly in front of the
// cell array, so "the cell above" of a top-row cell is really one of its entries. Pass the same state to consecutive floors to reproduce this.
class GameTables;
struct GenState {
    std::array<int32_t, 256> scan{};
    const GameTables* tables = nullptr;   // when set, item positions use the real SearchiDoPutArea (and consume exactly the game's rand() values)
    // Story state BuildCharaSpecialParts looks at (it puts the rooms where lost party members wait): floor number (0-based), party size (UserStatus + 5)
    // and the character-door state (UserStatus + 0x8b10, outside 0..5 for normal play). Its rand() draws are made in any case.
    int floor_no = 0;
    int party_size = 1;
    int chara_state = -1;
};

// Builds the layout for `seed`. `rooms` is the room count of the floor (second argument of buildRandomMap); `fresh` is its third argument
// (non-zero for a newly generated floor: dividers and stairs are placed). Returns the layout before the optional link-door and story-specific
// decorations that the game adds afterwards (they depend on the save game).
FloorLayout build_random_map(uint32_t seed, int rooms, int map_no, bool fresh = true, GenState* state = nullptr);

// For a fresh floor the game first draws seed = (int)(rand() / 100000.0f) from the running generator and calls srand(seed).
uint32_t derive_floor_seed(uint32_t rand_state);

// The PS2 FPU rounds toward zero; this is on by default. Switching it off (round to nearest) is only for checking which one the game uses.
void set_dungeon_round_to_zero(bool on);

}  // namespace dccore
