#pragma once
// The opening cutscene (notes/opening.md): the script opdat/opening.scr (26 commands), the camera / actor path files opdat/scene/*.sne and the stage tables
// of the title overlay (TITLE.BIN). `OpeningSim` runs the script exactly like CScript::Step (one step per main-loop iteration) and exposes the state the
// front end draws: stage objects, camera, fade, speech balloons and the one-shot requests (sounds, effects, scene switches).
#include <array>
#include <string>
#include <vector>
#include "dccore/bytes.h"
#include "dccore/disc.h"
#include "dccore/motion.h"

namespace dccore {

enum class OpKind {
    ObjMotion2, ObjMotion, ObjMove, ObjTalk, ObjEye, ObjMouth, ObjDisp, ObjStep, Camera, SeStop, Se, SoundFade, FadeIn, FadeOut, Message, NextMes, MesClr,
    LoadObj, Sprite, Bom, Beem, SceneLoad, Scene, End, WaitKey, Wait
};

struct OpCommand {
    OpKind kind = OpKind::End;
    std::array<float, 8> a{};
    int line = 0;
};

std::vector<OpCommand> parse_opening_script(const std::string& text);

// A .sne: the camera tracks of one cut and the placeholder nodes of its actors.
struct SneClip {
    std::string name;
    std::vector<std::string> node_names;
    std::vector<int> node_parent;
    std::vector<std::array<float, 16>> node_matrix;   // local, row-vector convention
    std::vector<MotTrack> tracks;
    bool camera_at(double t, float pos[3], float ref[3], float* fov) const;
    // The translation track of the node called `name` at t (or its matrix translation when it has none); false when the clip has no such node.
    bool node_at(const std::string& name, double t, float pos[3], float* yaw) const;
};
SneClip load_sne(View data, const std::string& name);

// The stage objects the cfg / tables of a scene place (TITLE.BIN tables of 0x20-byte entries).
struct OpStageEntry {
    std::string model, shadow;   // path inside the disc ("opdat/norn/e01h01_0.mds"); empty model = another instance of the previous one
    float pos[3] = {0, 0, 0};    // x10 already applied
    float rot_deg[3] = {0, 0, 0};
};
std::vector<OpStageEntry> opening_stage_table(const Bytes& title_bin, uint32_t address, int count);
// The names of the 125 camera files in the order the script's CAMERA commands play them.
std::vector<std::string> opening_sne_names(const Bytes& title_bin);

struct OpObject {
    bool disp = false;
    int motion = 0, motion2 = -1;
    bool move = false;
    int eye = 0, mouth = 0;
    float eye_time = 0, mouth_time = 0;
    bool talking = false;
    float step = 0.05f;           // cross-fade rate of motion changes
    int load_slot = -1;
    int motion_serial = 0;        // counts the motion commands (the front end restarts the motion when it changes)
};

struct OpBalloon {
    bool active = false;
    int msg = 0, speaker = -1;
    float life = 0, x = 0, y = 0, tx = 0, ty = 0;
    int page_limit = 0;           // NEXT_MES: auto page timeout
};

// One-shot things the script asked for during the last step.
struct OpEvent {
    enum Kind { Se, SeStop, SoundFade, Sprite, Bom, Beem, SceneLoad, Scene, LoadObj, Camera } kind = Se;
    std::array<float, 8> a{};
};

class OpeningSim {
public:
    OpeningSim(std::vector<OpCommand> cmds, std::vector<std::string> sne_names);
    // One main-loop iteration: CScript::Step, then the camera clip advances.
    void step();
    void press_ok();                    // cross / circle: closes the WAIT_KEY
    void skip();                        // cross while paused: the opening ends
    bool finished() const { return end_flag_; }
    bool waiting_key() const { return wait_key_; }

    // ---- state read by the front end
    int scene = 0;                      // 0 = part A, then the number of the last SCENE command
    std::array<OpObject, 23> obj;
    int cut = -1;                       // index of the current CAMERA command / .sne
    int cam_key = 0;
    float cam_start = 0, cam_end = 0, cam_speed = 1.0f, cam_time = 0;   // the camera clip time used for this step's picture
    const std::string& clip_name() const { return clip_name_; }
    float fade_alpha = 128.0f, fade_speed = 0.0f;     // 0..128, black or white
    bool fade_white = false;
    OpBalloon balloon;
    int sprite_id = 0;
    bool sprite_on = false;
    long steps = 0;
    std::vector<OpEvent> events;        // cleared at the start of every step

    bool auto_key = false;              // tools: a WAIT_KEY lasts two steps like in the timeline listing
    float wait_counter() const { return c_; }

private:
    void execute(const OpCommand& c, bool& stop);
    std::vector<OpCommand> cmds_;
    std::vector<std::string> sne_names_;
    size_t pc_ = 0;
    int key_steps_ = 0;
    float c_ = 0.0f;
    bool wait_key_ = false, end_flag_ = false;
    float t_ = 0;                       // clip time that is evaluated next
    std::string clip_name_;
};

}  // namespace dccore
