#pragma once
// Engine-independent assembly of a character/monster pack (.chr) into drawable surfaces: meshes baked to world space in the
// bind (model) pose, one surface per (part, texture), plus decoded textures and the joint hierarchy. Godot only converts this to scene nodes.
#include <array>
#include <map>
#include <string>
#include <vector>
#include "dccore/bytes.h"
#include "dccore/mds.h"
#include "dccore/motion.h"
#include "dccore/pack.h"
#include "dccore/tim2.h"

namespace dccore {

struct Surface {
    std::string texture;               // texture name inside the pack's .img ("" = untextured)
    std::string object;                // map models: the name of the MDS object (frame) the surface belongs to
    std::string path;                  // map models: the names of that object and its parents, outermost first, joined by '|' (a script that moves a frame moves its children too)
    float pivot[3] = {0, 0, 0};        // map models: world position of that object (the vertices are already in world space)
    bool additive = false;             // map objects named "effect..." (light glows) are drawn with additive blending
    std::vector<float> positions;      // xyz, in the OBJECT's own space (rigid parts: local to their joint; the skin: model space)
    std::vector<float> normals;        // xyz, unit length, same space
    std::vector<float> uvs;            // uv, v down (image row order)
    std::vector<uint32_t> indices;     // triangles, wound so that the front face is clockwise seen from outside (Godot convention)
    std::vector<uint32_t> source;      // index of each vertex in the MDS position block (to look up skin weights)
    std::vector<int32_t> bones;        // skinned parts only: 4 bone (= MDS object) indices per vertex
    std::vector<float> weights;        // skinned parts only: 4 weights per vertex, sum 1
};

struct Part {
    std::string name;                  // object name in the MDS (e.g. "skin", "kao", "boushi")
    int object = -1;                   // index into Character::joints
    bool skinned = false;              // true if the .wgt file deforms this object
    int skin_root = 0;                 // skinned parts: the object's parent; the game computes skin matrices relative to it (treated as identity)
    std::vector<Surface> surfaces;
};

struct Joint {
    std::string name;
    int parent = -1;
    std::array<double, 16> world{};    // row-vector convention, translation in the last row
};

struct Character {
    std::vector<Joint> joints;         // every MDS object (joints and parts); world = rest pose (MDS matrices)
    std::vector<Part> parts;
    std::map<std::string, Image> textures;
    std::string model_name, img_name;

    Skeleton skeleton;                 // rest + bind matrices (.bbp)
    std::vector<MotTrack> tracks;      // .mot key tracks (empty if the pack has no motion)
    std::vector<MotionEntry> motions;  // base.cfg KEY table
    // Events add motions to a model that is already there: a config without a MODEL line holds `MOTION n, "x.mot", ...` blocks with their own key numbers (KEY_START 250 ...).
    // Each block is one more motion file; its motions are found by their key number.
    struct ExtraMotion { int set = 0; MotionEntry entry; };
    std::vector<std::vector<MotTrack>> extra_sets;
    std::map<int, ExtraMotion> extra_motions;
    const MotionEntry* motion_entry(int no) const {   // the cfg row of motion `no` (extra blocks first), null when there is none
        auto it = extra_motions.find(no);
        if (it != extra_motions.end()) return &it->second.entry;
        return no >= 0 && size_t(no) < motions.size() ? &motions[size_t(no)] : nullptr;
    }
};

// True when the config names a model (MODEL "x.mds"); a config without one only adds motions to the character an event already has.
bool cfg_has_model(View cfg_text);
// Adds the MOTION blocks of such a config (their .mot files come from `pack`) to `ch`; returns how many motions were added.
int add_motion_sets(Character& ch, const Pack& pack, View cfg_text);

// Values of a config line like  MODEL "c01d.mds"  /  IMG 0,"c01d01_dun.img"  (first quoted string of each matching line, comments stripped).
std::vector<std::string> cfg_strings(View cfg_text, const std::string& keyword);

// Builds a character from separate files (the opening keeps the model, the skeleton and the motions in different packs); the textures are the caller's.
Character load_character_parts(View mds_data, View bbp_data, View mot_data, View wgt_data, View cfg_text, const std::string& model_name);

// Builds the character from a pack that contains base.cfg (or, failing that, the first non-shadow .mds and the first .img).
Character load_character(const Pack& pack, const std::string& cfg_entry = "");   // cfg_entry: the pack holds several characters, use this config

}  // namespace dccore
