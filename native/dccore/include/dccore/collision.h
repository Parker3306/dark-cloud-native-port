#pragma once
// Map collision: the polygon soup of a floor and the game's own character movement queries (MoveCheck, CheckWidth, GetFootPoly, CheckHit...).
// Ported from the decompiled functions at 0x149C30-0x14B830 (main ELF); verified against the running game (tools/dc/validate_move.py).
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

// The game's CCPoly: 0x50 bytes = three vertices (vec4), the plane normal (vec4, NOT normalised when it comes out of a collision mesh) and a 16-byte attribute.
// Attribute bytes: the 16-bit value at byte 6 of the attribute is the "type" mask that the queries filter on (`(type & mask) == 0` -> polygon is considered).
struct CCPoly {
    float v[3][4];
    float n[4];
    uint32_t attr[4];
    int type() const { return int(int16_t(attr[1] >> 16)); }
};
static_assert(sizeof(CCPoly) == 0x50, "CCPoly must match the game's layout");

// One collision piece (a `*_a.mds` file): its polygons in the piece's own space (object matrices baked in).
struct CollisionPiece {
    std::vector<CCPoly> polys;
    std::vector<uint8_t> door;   // parallel to polys: kind of the sliding door the polygon belongs to (see door_kind_of), 0 for fixed geometry
    std::vector<std::pair<std::string, std::array<float, 3>>> frames;   // every object of the file with its position in the piece's space (trigger markers are empty objects)
};

// Parses a collision mesh. The file is an MDS container whose meshes use a special group list: after the 16-byte group header comes {type, count} and
// `count` polygons of five ints (vertex a, b, c, attribute index, spare); attributes are 16-byte records in the material block.
CollisionPiece parse_collision_mds(View data);

// Row-vector 4x4 used by the game for frames (rotation about Y by -90 degrees * turn, then translation).
struct Placement {
    float yaw_turns = 0;   // quarter turns as the game counts them (see model_turn_index)
    float pos[3] = {0, 0, 0};
};

// Transforms a piece into world space with the game's placement (normals rotated, not renormalised).
// Doors that open on an event: 1 locked door ("ndoor*", opens with the door key), 2 exit gate ("out_door*", opens with the gate key), 4 character door ("cdoor*"), 0 anything else.
int door_kind_of(const std::string& object_name);
// skip_kinds: bit mask of door kinds whose polygons are left out (doors that were opened).
void place_polys(const CollisionPiece& piece, int turn, const float pos[3], std::vector<CCPoly>& out, int skip_kinds = 0);

// ---- the queries -------------------------------------------------------------------------------------------------------------------------------

// Segment p1->p2 against the polygons (first hit in list order, or the nearest to p1 when `nearest`). Returns the polygon index or -1; `hit` receives the point.
int check_hit(const CCPoly* polys, int n, const float p1[3], const float p2[3], float hit[3], bool nearest, int mask);

// All hits of the segment (up to max_hits), optionally sorted by distance from p1 (sort > 0 ascending, < 0 descending). Returns the count.
int check_hits(const CCPoly* polys, int n, const float p1[3], const float p2[3], int max_hits, int* index_out, float (*hit_out)[4], int sort, int mask);

// Vertical probe from `p` upwards by `up` (down if negative): picks the closest hit to the start in the direction of travel. Returns the index or -1.
int check_hit_vertical(float up, const CCPoly* polys, int n, const float p[3], float hit[3], int mask);

// Pushes `pos` out of walls with a body radius `r` (8 probes around it); writes the corrected position to `out`. Returns non-zero if a wall was touched.
int check_width(float r, const CCPoly* polys, int n, const float pos[3], float out[3], int mask);

// The floor under `p` (ray down by `range`). Returns true and fills the polygon and the hit point.
bool get_foot_poly(float range, const float p[3], CCPoly& poly, float hit[3], const CCPoly* polys, int n, int mask);

struct MoveInfo {
    int grounded = 0;        // feet on a floor
    int foot_found = 0;      // a floor polygon was found under the character
    CCPoly foot;             // that polygon (normal normalised)
    float foot_hit[3] = {0, 0, 0};
};

// One movement step: `vel` is the wanted displacement of this tick (x, y, z; modified when blocked), `out` the resulting position. Returns true if a
// wall/obstacle was hit on the way. `mask`: polygon types to ignore (1 for everybody but Osmond, who uses 8).
bool move_check(const float pos[3], float vel[3], float out[3], MoveInfo& info, CCPoly* polys, int n, int mask);

}  // namespace dccore
