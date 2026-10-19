#pragma once
// Dungeon floor assets: the per-dungeon .cfg (part table + environment), the pieces inside the .mpd pack, and the placement of pieces on a generated
// floor (dungeon.h). Pieces are baked to the pack's root space; a cell is 160 units wide and cell (x, y) is centred at world (x * 160, 0, y * 160).
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "dccore/character.h"
#include "dccore/collision.h"
#include "dccore/dungeon.h"
#include "dccore/mds.h"
#include "dccore/pack.h"

namespace dccore {

constexpr float kCellSize = 160.0f;

struct PartModelRef {
    std::string file;       // PT_BASE: model file inside the .mpd
    int slot = 0;           // second field (texture/VRAM slot used by the PS2 renderer)
    float offset[3] = {0, 0, 0};
    int rot = 0;            // extra quarter turns for this model only
};

struct PartRef {            // PT_COLS / PT_CAM: collision and camera mesh with their own rotation
    std::string file;
    int rot = 0;
};

struct PartMarker {         // PT_MARKER "name", radius, event id, use item, link, switch, extra event: a trigger area on a named frame of the part's model
    std::string name;
    float radius = 0;
    int event_id = -1;      // function of the dungeon's event.stb that runs when the player stands in the area
    bool use_item = false, link = false, sw = false, extra = false;
    // PT_HIT_MARKER "name", radius, event, chara, attack, switch, ext: the event starts when a hit sphere of character `chara` (and attack id `attack`, -1 any, -2 the Ruby key) reaches the
    // area; OK near it only asks for a hint (the Goro / Ruby / Ungaga rooms)
    bool hit = false;
    int chara = -1, attack = -1;
};

struct PartDef {            // one DEF_PATS block; its index in DungeonCfg::parts is the part number used by the floor grid
    std::vector<PartModelRef> models;
    std::vector<PartRef> collisions, cameras;
    std::vector<std::array<float, 3>> fires;  // torch positions (model space)
    std::vector<PartMarker> markers;
};

struct CfgLight {
    float dir[3] = {0, 0, 0};
    float color[3] = {0, 0, 0};  // 0..255
    int index = 0;
};

struct CfgEnv {
    float ambient[3] = {0, 0, 0};
    std::vector<CfgLight> lights;
    float fog_near = 0, fog_far = 0;
    float fog_color[3] = {0, 0, 0};
    float bg[3] = {0, 0, 0};
};

struct DungeonCfg {
    std::string path;           // SET_PATH
    CfgEnv front, back;         // normal floor / "ura" (back) floor
    float view_level = 0;
    std::string ground_img;     // GRD_IMG: texture container of the floor pieces
    std::vector<std::string> drans_parts, drans_cols;   // DRANS_PARTS / DRANS_COLS (d01boss): the 12 tiles of Dran's arena floor (models with a crumbling motion and their collision)
    std::string minimap_img;    // MINIMAP_IMG: the mini map tiles (16 x 16, 8 per row); a part's first tile is its PT_BASE slot value
    std::vector<PartDef> parts;
};

DungeonCfg parse_dungeon_cfg(View text);

// A piece baked into root space (all objects' world matrices applied); surfaces are grouped by texture.
struct StaticModel {
    std::vector<Surface> surfaces;
    std::map<int, std::vector<Surface>> door_surfaces;   // by door kind (door_kind_of): the sliding doors of locked doors / the exit gate, drawn separately so they can disappear
};
StaticModel build_static_model(const Mds& mds);

// Quarter-turn index of a model on a cell, with the game's own wrap-around (Draw/DrawCalc of CDungeonParts): angle = -90 degrees * index about +Y.
int model_turn_index(int cell_rot, int model_rot);

struct Dungeon {
    DungeonCfg cfg;
    std::map<std::string, StaticModel> models;       // by file name inside the .mpd
    std::map<std::string, Image> textures;           // all textures of the ground .img
    std::vector<std::string> failed_models;          // pieces the readers could not parse (reported, not fatal)
    std::map<std::string, CollisionPiece> collisions; // PT_COLS meshes (`*_a.mds`) by file name
    std::vector<std::shared_ptr<Character>> drans;      // the arena tiles (mot01.chr ..)
    std::map<std::string, CollisionPiece> cam_meshes; // PT_CAM meshes (`*_v.mds`, camera collision) by file name
    // Positions (model space) of the objects a PT_MARKER names, by model file and object name.
    std::map<std::string, std::map<std::string, std::array<float, 3>>> marker_frames;
};

// World-space collision polygons of the (2 * radius + 1)^2 cells around world position (x, z), in the game's order (row by row), each piece placed with
// the cell's rotation plus the PT_COLS rotation. Radius 1 is what the game gathers around the player.
// Cells whose locked door was opened (FloorLayout::door_open) leave out the polygons of their sliding doors.
std::vector<CCPoly> floor_polys_near(const Dungeon& d, const FloorLayout& layout, float x, float z, int radius = 1);

// Camera collision polygons: the `_v` pieces of every cell whose centre is within 240 units (horizontally) of (x, z) -- the game's draw-distance test.
std::vector<CCPoly> floor_camera_polys(const Dungeon& d, const FloorLayout& layout, float x, float z);

// A trigger area of a placed part (CDungeonEventMan::SetupEvent): world position of the marker's frame.
struct FloorMarker {
    int cell_x = 0, cell_y = 0;
    PartMarker marker;
    float pos[3] = {0, 0, 0};
};
// The named frame (an empty object of a part's collision mesh or model: `chr1_3`, `u03_top` ...) of the placed floor: its world position and the heading of the part's +z.
// False when no placed part has it. (_GET_OBJHDL with part -1.)
bool find_floor_frame(const Dungeon& d, const FloorLayout& layout, const std::string& name, float pos[3], float* yaw);
std::vector<FloorMarker> floor_markers(const Dungeon& d, const FloorLayout& layout);
FloorLayout make_fixed_layout(int map_no);
std::array<float, 3> fixed_start(const Dungeon& d);
// Where the torches (PT_FIRE of the parts) stand in the world.
std::vector<std::array<float, 3>> floor_fires(const Dungeon& d, const FloorLayout& layout);

// Builds the dungeon from the text of `dun\<name>.cfg` and the pack `dun\mpd_pack\<name>.mpd` (e.g. name = "d01main_a").
Dungeon load_dungeon(const std::string& cfg_text, const Pack& mpd);

}  // namespace dccore
