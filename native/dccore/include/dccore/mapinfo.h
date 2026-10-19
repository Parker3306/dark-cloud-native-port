#pragma once
// mapinfo.cfg of the Georama towns and story scenes (data/gedit/<map>/mapinfo.cfg): the game's script interpreter (CScriptInterpreter, GetNextTAG /
// SearchCommand / GetArg) with its table of 61 directives, and what the Command* handlers do with the values. Ported from tools/dc/mapinfo.py;
// the format is described in notes/georama.md section 4.
#include <array>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

struct MapLight {
    float ambient[3] = {0, 0, 0};
    bool has_dir[4] = {false, false, false, false};
    float dir[4][3] = {};      // normalised, pointing towards the light
    float color[4][3] = {};    // 128 = full
    bool has_fog = false;
    float fog[7] = {};         // near far r g b alpha_near alpha_far
    bool has_bg = false;
    float bg_top[3] = {0, 0, 0}, bg_bottom[3] = {0, 0, 0};
};

struct MapAnim {              // OBJ_ANIME
    int kind = 0, mode = 0;
    std::string target;
    float init[3] = {}, delta[3] = {}, end[3] = {};
};

struct MapEvent {             // MAPJUMP (+ ENTRANCE)
    int jump_id = 0;
    std::string map;
    bool has_door = false;
    int side = 0;             // -1 left, 1 right, 0 other
    float door_pos[3] = {}, door_rot[3] = {}, extent[3] = {};
};

struct MapOwned {             // what hangs on the last SKY / SUN / GROUND / BUILD / WATER / *_PARTS
    std::vector<int> anims;   // 1-based indices into MapInfo::anims
    std::vector<int> events;  // 1-based indices into MapInfo::events
};

struct MapObject : MapOwned { // GROUND / BUILD / WATER
    std::string kind;
    std::vector<std::string> names;   // 8 file names (already prefixed with CD); the first one is also the SCN entry name
    float pos[3] = {}, rot_deg[3] = {};
};

struct PartsInfoDef {
    int width = 0, height = 0;
    std::string grid;
    int max_count = 0;
    std::array<std::pair<int, std::string>, 6> elements;
};

struct PartsDef : MapOwned {  // BLD_PARTS ... ON_RIVER_PARTS
    int index = 0;
    std::string directive, file, extra;
    int flag = 0;
    float scale = 0;
    bool has_info = false;
    PartsInfoDef info;
};

struct EditAreaDef {
    std::string grid;
    int width = 0, height = 0;
    float unit = 0, unit_alt = 0, origin[3] = {};
};

struct TextureRef {
    std::string dir, file;
    int type = 0, flag = 0;
};

struct SkyDef : MapOwned {
    std::string file;
    int flag = 0;
};

struct WaterSurfaceDef {
    std::string texture;
    int nx = 0, nz = 0;
    float p0[3] = {}, p1[3] = {}, origin[3] = {}, params[4] = {}, rgb[3] = {};
    int extra[3] = {};
    std::vector<std::array<float, 4>> shakes;
};

struct MotionPartDef {
    std::string file;
    float pos[3] = {}, rot[3] = {};
};

struct PeopleDef {
    std::string name;
    int part = -1, element = -1, indoor_only = 0, moves = 0;
    float speed = 0, pos[3] = {}, rot_y = 0;
    int talk_event[2] = {0, 0};
    bool has_talk_event = false;
    int talk_rot = -1, talk_dir = -1;
};

struct MapInfo {
    std::string scn = "scene.scn";
    int bgm = -1, sound_set = -1;
    bool time_stop = false, se_ambient_off = false;
    std::array<MapLight, 12> lights{};
    bool has_dof = false;
    float dof[7] = {};
    int sky_follow[3] = {1, 0, 1};
    std::vector<TextureRef> textures;
    std::map<int, SkyDef> skies, suns;
    std::vector<MapObject> objects;
    std::vector<WaterSurfaceDef> water_surfaces;
    std::map<int, EditAreaDef> edit_areas;
    std::vector<std::array<float, 6>> edit_rects;     // min xyz, max xyz
    std::map<int, PartsDef> parts;
    std::map<int, std::tuple<std::string, int, float>> roads;                   // ROAD n -> file, flag, scale
    std::map<int, std::tuple<std::string, int, float, std::string>> rivers;     // RIVER n -> file, flag, scale, water model
    std::vector<MapAnim> anims;
    std::vector<MapEvent> events;
    std::vector<MotionPartDef> motion_parts;
    std::vector<PeopleDef> people;
    std::map<std::pair<int, int>, std::array<int, 6>> time_table;               // (week, slot) -> villagers
    std::vector<int> people_list;
    float chara_ambient[4][3] = {};
    bool has_chara_ambient[4] = {};
    std::string reverb[2];
    float projection = 800.0f;                          // interior configs: the camera's focal length
};

// Parses the text exactly like the game (comments, word skipping, argument rules). Throws Error on a malformed argument (the game would loop forever).
MapInfo parse_mapinfo(View text, bool interior = false);   // interior: the 15 directive table of the house configs (LoadInfo)

}  // namespace dccore
