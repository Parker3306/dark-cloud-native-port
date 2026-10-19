#pragma once
// Putting things on a floor at random: the game's SearchiDoPutArea (item/monster areas of the dungeon pieces).
#include "dccore/dungeon.h"
#include "dccore/tables.h"

namespace dccore {

struct PutPosition {
    float x = 0, y = 0, z = 0, w = 1;
    int areas = 0;       // how many boxes were found in the rectangle (the function's return value)
};

// Picks a random position inside the free-area boxes of the cells of rectangle (rx, ry, rw, rh) (cell coordinates). Draws three rand() values: the box, x, z.
PutPosition search_put_area(const FloorLayout& layout, const GameTables& tables, int map_no, int rx, int ry, int rw, int rh, GameRand& rng);

}  // namespace dccore
