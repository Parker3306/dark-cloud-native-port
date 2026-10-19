#pragma once
// Georama editing of a town: placing, removing and reshaping the parts the player builds (CEditGround::SetMapParts / DeleteMapParts, CEditArea::CheckParts,
// SetRoadParts / SetRiverParts). The state is in Town (slots, cells of the edit areas, instances); every change is also logged in Town::edit_log.
// Rules: notes/georama_edit.md section 4.5.
#include <array>
#include <vector>
#include <string>
#include "dccore/town.h"

namespace dccore {

int edit_part_kind(const PartsDef& def);   // 0 building / ground, 1 road, 2 river, 3 bridge, 4 lake, 5 on-river
int edit_find_area(const Town& t, const float pos[3]);   // GetAreaCode: the first edit area whose rectangle holds the point, -1 none
void edit_cell(const Town::Area& a, const float pos[3], int& cx, int& cz);   // GetPos: the cell that holds the point
// Where a part ends up when the cursor is in cell (cx, cz): the footprint centre (cell corner, plus half a cell when the rotated width / height is odd).
std::array<float, 3> edit_part_position(const Town& t, int area, int cx, int cz, int def, int rot);
// The centre of a cell (for the D-pad steps of the cursor), at the cell's altitude.
std::array<float, 3> edit_cell_center(const Town::Area& a, int cx, int cz);
// The cells a part covers around its centre cell, rotated: {dx, dz, code} with code 0 = not part of the footprint (for the outline).
std::vector<std::array<int, 3>> edit_footprint_cells(Town& t, int def, int rot);
// CheckParts: may the part stand here? (the copy limit is not part of it)
bool edit_check_parts(const Town& t, int def, int rot, int area, int cx, int cz, std::string* why = nullptr);
// SetMapParts: puts the part into the first free slot; `check` = false is for loading a saved layout. Returns the slot or -1. `pos` is the cursor point
// (check) or the saved footprint centre (no check).
int edit_set_part(Town& t, int def, int rot, const float pos[3], bool check, std::string* why = nullptr);
// DeleteMapParts of a slot: clears its cells, re-shapes its neighbours; bridges and on-river parts turn back into a river piece. False if refused.
bool edit_delete_slot(Town& t, int slot);
// The slot of the part standing on the cell under a point (-1 none).
int edit_slot_at(const Town& t, const float pos[3]);
// Re-picks the road / river variant of every such cell (after a load).
void edit_reshape_all(Town& t);
// Unrotated footprint size of a part type (false if unknown).
bool edit_footprint_size(Town& t, int def, int& w, int& h);
// Is that river cell one of the fixed river entry / exit cells of Matataki (not removable)?
bool edit_protected_cell(const Town& t, int area, int cx, int cz);

}  // namespace dccore
