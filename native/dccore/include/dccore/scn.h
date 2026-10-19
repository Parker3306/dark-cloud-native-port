#pragma once
// scene.scn ('SCN' container) and the 'PTS' map parts inside it (towns, story scenes, house interiors). Ported from tools/dc/pts.py; the formats are
// described in notes/georama.md sections 2 and 3.
#include <array>
#include <string>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

// The model slots of a PTS and what they are for.
enum PtsSlot { kPtsLod0 = 0, kPtsLod1, kPtsLod2, kPtsLod3, kPtsShadow, kPtsCollision, kPtsShade, kPtsWater, kPtsCamera, kPtsSlots };

// Types of the function records (EPARTS_FUNC_DATA +0x10).
enum PtsFuncType {
    kFnPerson = 1, kFnMapJump = 2, kFnFire = 3, kFnSmoke = 4, kFnLight2 = 5, kFnObjAnime = 6, kFnCameraZone = 7, kFnTimer = 9, kFnDoorR = 10, kFnDoorL = 11,
    kFnFlame = 12, kFnLight = 13, kFnPointLight = 14, kFnCandle = 15, kFnMapJumpCamp = 16, kFnItem = 17, kFnEvent = 18, kFnLadderBottom = 19,
    kFnLadderTop = 20, kFnSoundSrc = 21
};

struct PtsFunc {
    std::string label;
    int type = 0;
    float t_start = 0, t_end = 24;      // clock hours; active in [start, end), wrapping over midnight
    int id = 0, flag = 0, enabled = 1;
    std::string target;                 // frame (object) of the part's models the record is bound to
    float pos[3] = {}, rot[3] = {}, scale[3] = {}, params[4] = {};
    float matrix[16] = {};              // +0x80: the editor's world matrix of the record (camera zones use it)
};

struct PtsInfo {
    int width = 0, height = 0, max_count = 0, f14 = 0, f1c = 0;
    float offset[3] = {}, rotation[3] = {};
    int element_ids[6] = {-1, -1, -1, -1, -1, -1};
    std::string elements[6];            // space separated frame names that belong to element k ('-' prefix: shown when NOT installed)
    Bytes grid;                         // width * height: 0 free, 1..9 raises the ground, 0x80 occupied, 0x81 occupied water
    std::vector<PtsFunc> funcs;
};

struct Pts {
    std::string name;
    Bytes block;
    struct Model {
        std::string name;
        size_t offset = 0, size = 0;
    } models[kPtsSlots];
    PtsInfo info;
    View model(int slot) const { return View(block).subspan(models[slot].offset, models[slot].size); }
    bool has(int slot) const { return models[slot].size != 0; }
};

struct Scn {
    std::vector<Pts> parts;
    const Pts* find(const std::string& name) const;   // SearchPTS: leading path stripped, first match wins
};

Scn parse_scn(View data);

// StrCmp's rule for frame names: only the part before the first "__" counts.
std::string frame_key(const std::string& name);

}  // namespace dccore
