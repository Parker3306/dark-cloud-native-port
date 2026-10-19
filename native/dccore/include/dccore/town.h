#pragma once
// A Georama town or story scene (data/gedit/<map>): the static objects of mapinfo.cfg, the parts the player placed (houses, trees, roads...), their
// models, collision and trigger points. See notes/georama.md.
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "dccore/collision.h"
#include "dccore/disc.h"
#include "dccore/floor.h"
#include "dccore/mapinfo.h"
#include "dccore/scn.h"

namespace dccore {

// One model with everything the game keeps next to it.
struct TownAsset {
    std::string name;
    StaticModel lod[4];                  // LOD0..LOD3 (a missing level is empty)
    StaticModel water;                   // slot 7 (ripples of lakes and rivers)
    CollisionPiece col, cam;
    bool has_col = false, has_cam = false;
    PtsInfo info;                        // footprint, elements, function records (empty for loose model files)
    bool is_pts = false;
};

// An asset put into the world.
struct TownInstance {
    int asset = -1;
    float pos[3] = {0, 0, 0};
    float yaw = 0;                       // radians about +Y (right handed, the game's SetRotation)
    int part_def = -1;                   // index of the BLD_PARTS... definition for placed parts, -1 for static objects
    int owner_object = -1;               // index into MapInfo::objects for static objects
    int area = -1;                       // placed parts: the edit area
    int rot = 0;                         // placed parts: rotation index (0, 1, 2, -1)
    float bbox_min[3] = {}, bbox_max[3] = {};   // world bounds of the collision (or of LOD0)
    bool visible = true;
    bool removed = false;                // a part that was taken away (the slot of the instance list stays)
};

// A point the player can use (ED_EVENT_POINT).
struct TownEvent {
    enum Kind { MapJump = 1, Item = 2, Event = 3, LadderBottom = 4, LadderTop = 5 };
    int kind = 0;
    std::string map;                     // MapJump: destination ("i01h01" interior, "dungeon", another map)
    int jump_id = 0;
    float pos[3] = {}, yaw = 0, extent = 10;
    float out_pos[3] = {};
    bool has_out = false;
    int door_side = 0, door_style = 0;
    int flag = 0;                        // map flag that disables the point once set
    bool initially_active = true;        // the record's +0x28: the flag starts out 'not set' when 1
    int event_no = 0;                    // function of event.stb to run (> 0)
    int item = 0, count = 0, min_progress = 0;
    float t_start = 0, t_end = 24;
    int instance = -1;
};

// Where a villager stands.
struct TownPerson {
    int villager = 0;
    float pos[3] = {}, yaw = 0;
    int instance = -1;
};

struct PlacedPart {
    int part = 0;                        // BLD_PARTS ... index
    int rot = 0;                         // 0, 1, 2, -1 quarter turns
    float pos[3] = {};
};

// The developer's complete layout (gdata0.edt) or a layout from the save data.
std::vector<PlacedPart> parse_gdata_edt(View data);

// A fixed camera of a house interior (function record type 7): while the hero is inside the box the camera stands at `cam_pos` and looks at him.
struct CameraZone {
    int id = 0;                          // 0: the default camera (used when no other zone contains the hero)
    float box_min[3] = {}, box_max[3] = {};
    double to_local[16] = {};            // world -> the zone's space (row vectors)
    double cam_pos[3] = {};
};

// One of the 128 slots of placed parts (CEditGround's part list): which part type, where, and the instance that shows it.
struct EditSlot {
    bool used = false;
    int def = -1, rot = 0;               // part type (BLD_PARTS ... index) and rotation index (0, 1, 2, -1)
    float pos[3] = {0, 0, 0};            // centre of the footprint, as saved
    int instance = -1, area = -1, cx = 0, cz = 0;
    int variant = -1;                    // roads and rivers: the shape variant shown (shape - 1)
};

// What an editing operation changed (the scene follows these).
struct EditChange {
    enum Kind { Add, Remove, Update } kind = Add;
    int instance = -1;
};

struct Town {
    std::string name;                    // "e01"
    std::string dir;                     // "gedit/e01/"
    MapInfo info;
    Scn scn;
    std::vector<TownAsset> assets;
    std::vector<TownInstance> instances;
    std::vector<TownEvent> events;
    std::vector<TownPerson> people;
    std::map<std::string, Image> textures;
    std::vector<std::string> water_textures;   // the textures of the WATER_IMG containers in order (the first one covers a WATER_SURFACE without a name)
    std::map<int, StaticModel> skies, suns;   // SKY 1..5 / SUN 1..4 (baked; the cfg's OBJ_ANIMEs that turn their frames are not applied)
    std::vector<std::string> failed;     // models that could not be read (reported, not fatal)
    std::vector<PlacedPart> layout;
    // edit areas: per cell the part number placed there (-1 free) and its code (0x80 occupied, 0x81 water)
    struct Area {
        EditAreaDef def;
        std::vector<int> part;
        std::vector<uint8_t> code;
        StaticModel grid;                    // the area's ground model (EDITAREA's file: one quad per cell, drawn at the origin of the area)
        std::vector<int8_t> alt;             // altitude steps per cell (terrain raised by mounds)
        std::vector<int16_t> slot;           // placed slot per cell (-1 none)
        std::vector<uint8_t> kind;           // part kind per cell: 0 none/building, 1 road, 2 river, 3 bridge, 4 lake, 5 on-river
    };
    std::vector<Area> areas;
    // house interiors
    bool interior = false;
    std::string parent;                  // the town the house belongs to ("e01")
    std::vector<CameraZone> cam_zones;
    std::array<EditSlot, 128> slots;
    std::vector<EditChange> edit_log;     // changes since the scene last looked (cleared by the consumer)
    std::map<std::string, int> asset_by_key;   // asset index by part file stem
    std::vector<std::string> villager_names;   // model names of the town's villagers by number (PEOPLE2 order)
};

// A house of a town: gedit/<town>/in/<house>/ with the room of the time of day (variant 0 day, 1 evening, 2 night).
Town load_interior(const Disc& disc, const std::string& town, const std::string& house, int variant);

// The camera switching of SetCameraPos: remembers the active zone and only changes after the hero stayed in another one for a while. Returns false if the
// interior has no camera zone.
struct InteriorCamera {
    int active = -1, counter = 0;
    bool update(const Town& t, const float hero[3], float cam_pos[3]);
};

// Builds / removes the trigger points and standing points of one instance (placing or removing a part rebuilds its doors and villager spots).
void build_instance_triggers(Town& t, size_t instance);
void remove_instance_triggers(Town& t, size_t instance);

// Index of the asset for a part file of the scene (loads it on first use), -1 if the scene has no such part.
int town_asset(Town& t, const std::string& file);

// Loads the map `name` ("e01".."e05", "s01", ...) and places `layout` (empty: only the static objects).
Town load_town(const Disc& disc, const std::string& name, const std::vector<PlacedPart>& layout);

// The collision polygons the game gathers around a position (PickUpPoly): the collision meshes of nearby parts and objects and the flat cell polygons.
std::vector<CCPoly> town_polys_near(const Town& t, float x, float y, float z);
// The camera collision polygons (PickUpCameraPoly).
std::vector<CCPoly> town_camera_polys(const Town& t, float x, float y, float z);

}  // namespace dccore
