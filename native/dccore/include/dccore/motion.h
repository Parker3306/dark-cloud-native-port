#pragma once
// Skeletal animation: .mot key tracks, .bbp bind matrices, the motion table of base.cfg, pose evaluation and linear-blend skinning.
// Everything here was read off the game's own code: MotionProc / MotionProc2 / SetMotionEX / CreateAnimeDataEX / QuatToMat / QuatSlerp
// (see notes/native-plan.md, "Milestone 2"). Matrices are 4x4 row-major in the row-vector convention (p' = p * M, translation in the last row).
#include <array>
#include <string>
#include <vector>
#include "dccore/bytes.h"
#include "dccore/mds.h"
#include "dccore/wgt.h"

namespace dccore {

using Mat4 = std::array<double, 16>;

Mat4 mat_identity();
Mat4 mat_mul(const Mat4& a, const Mat4& b);  // a * b: apply a first, then b (so world = local * parent_world)
Mat4 mat_inverse(const Mat4& m);             // general 4x4 inverse

// One key track of a .mot file. Same record chain layout as .wgt: 0x20-byte header (object, param, type, unk, count, more) + count * 0x20-byte keys.
struct MotKey {
    float time;     // frame number (stored in the file as a u32)
    float v[4];     // rotation: quaternion (w, x, y, z) of the inverse rotation (the game negates w); translation/scale: xyz
};
struct MotTrack {
    uint32_t object;  // index of the MDS object (frame) this track drives
    uint32_t param;
    uint32_t type;    // 0 rotation, 1 scale, 2 translation; 0xc vertex animation; 0x1e..0x21 camera; 0x28/0x29 material; 0x32/0x33 visibility flags; 200 = skip
    std::vector<MotKey> keys;
};
std::vector<MotTrack> parse_mot(View data);

// The camera tracks of a scene motion (types 0x1e position, 0x1f target, 0x21 field of view in degrees) at a frame; false if the motion has none.
bool sample_camera_tracks(const std::vector<MotTrack>& tracks, double time, float pos[3], float ref[3], float* fov);

// .bbp: one local bind matrix (64 bytes) per MDS object.
std::vector<Mat4> parse_bbp(View data);

// base.cfg "KEY start, end, speed" lines (between KEY_START and MOTION_END): motion number -> frame range and frames advanced per game tick.
struct MotionEntry {
    float start = 0, end = 0, speed = 0;
};
std::vector<MotionEntry> cfg_motion_table(View cfg_text);

struct Skeleton {
    std::vector<std::string> names;
    std::vector<int> parent;
    std::vector<Mat4> rest_local;   // MDS object matrices
    std::vector<Mat4> bind_local;   // .bbp
    std::vector<Mat4> bind_world;   // bind_local chained along the parents
    std::vector<Mat4> bind_inverse; // inverse of bind_world
    size_t size() const { return parent.size(); }
};
Skeleton make_skeleton(const Mds& mds, const std::vector<Mat4>& bbp);  // bbp may be empty (then bind = rest)

struct Pose {
    std::vector<Mat4> local, world;
};
// Pose at a motion time (frame number, may be fractional). Objects without a track keep their rest matrix.
Pose evaluate_pose(const Skeleton& sk, const std::vector<MotTrack>& tracks, double time);
// Pose while the game changes motion (MotionProc with next != frame + 1): every track is interpolated between its key at or before `frame` and its key at or
// before `next`, by `frac`.
Pose evaluate_pose_blend(const Skeleton& sk, const std::vector<MotTrack>& tracks, double frame, double next, double frac);
Pose rest_pose(const Skeleton& sk);

// Linear blend skinning exactly as MotionProc2 does it: p' = p + sum_b (w_b/100) * (p * M_b - p) with M_b = B_b^-1 * A_b (B = bind world, A = animated world).
// The game computes both chains relative to the skin object's parent (`root`, usually object 0), which it treats as identity: the root object's own
// animated translation moves the drawn frames but never the skinned vertices. skin_matrices() returns M_b for every bone.
struct Vec3d { double x, y, z; };
std::vector<Mat4> skin_matrices(const Skeleton& sk, const Pose& pose, int root);
// weights: bone -> percent for one vertex; rest = vertex position in the skin object's space.
Vec3d skin_point(const Vec3d& rest, const std::map<uint32_t, float>& bones, const std::vector<Mat4>& mats);

}  // namespace dccore
