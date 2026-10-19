#pragma once
// A dungeon's event script (dun/script/d0N/event.stb) is the same bytecode as the monsters' scripts but uses the "event" set of external functions
// (cut-scene commands, flags, map selection). Function 100 is the dungeon's main script: it chooses which map file a floor uses.
#include <functional>
#include <string>
#include "dccore/bytes.h"

namespace dccore {

struct FloorMapChoice {
    std::string name;     // file stem of the map ("d01main_a", "d01inter", "d01boss", ...), empty when the script chose nothing
    int kind = 1;         // 1 random floor, 0 fixed story map, 2 boss map (third argument of _SET_DUNGEON_MAP)
    int zone = -1;        // the limit zone the script set (_SET_LIMMIT_ZONE: 0..5 = only that character may play the floor, 10 weapons wear, 11 thirst); -1 none
};

// Runs function 100 of the dungeon script for floor `floor` (0-based, UserStatus + 2) and reports the last map it selected.
// `game_int` / `game_flag` answer _SGET_GAME_INT_FLAG / _SGET_GAME_FLAG (the story state some dungeons read: dungeon 6 picks its boss maps by game int 10).
FloorMapChoice choose_floor_map(const Bytes& event_stb, int floor, const std::function<int(int)>& game_int = nullptr, const std::function<bool(int)>& game_flag = nullptr);

}  // namespace dccore
