#pragma once
// The motion clock of a CCharacter (SetMotionEX / CCharacter::Step): which motion plays, at which frame, and how a change of motion blends. Used by monsters and the
// player; the pose itself is evaluated from the frame number elsewhere (motion.h).
#include <cstdint>
#include <vector>

namespace dccore {

// One entry of a motion table (base.cfg KEY line): frame range and frames advanced per tick.
struct MotionInfo {
    int start = 0, end = 0;
    float speed = 0;
};

// Motion request that game code writes and CCharacter::Step consumes (CCharacter +0xc60 / +0xc64 / +0xc68).
struct MotionRequest {
    float speed = -1.0f;      // > 0 overrides the table speed
    uint32_t flags = 0;       // 1 freeze, 2 hold the last frame, 4 restart without blending (cleared after use)
    int id = 0;               // motion number, < 0 = no motion
};

// MOTION_STATE of the CCharacter's motion slot.
struct MotionState {
    float time = 0;           // [0] current frame (fractional)
    float frac = 0;           // [1] blend position while changing motion
    float blend_step = 0.1f;  // [2] blend speed per tick
    int frame = 0, next = 0;  // [3] [4]
    int cur = 0, prev = 0;    // [5] [6] motion number now / at the last SetMotionEX
    int blending = 0;         // [7]
};

// Which pose SetMotionEX evaluated in a step: at `time` when the motion plays normally, or (while changing motion) between the keys at `frame` and at `next` by `frac`.
struct PoseSample {
    bool blend = false;
    float time = 0;
    int frame = 0, next = 0;
    float frac = 0;
};

// One CCharacter::Step: applies the request and advances the time. `phase` receives CCharacter +0xc70 (0 nothing, 1 motion changed, 2 playing, 3 on the last frame).
// Returns the frame the pose of this step belongs to (the time before it advanced), or -1 when there was nothing to play.
float step_motion_state(std::vector<MotionInfo>& table, MotionRequest& request, MotionState& state, int& phase, PoseSample* sample = nullptr);

}  // namespace dccore
