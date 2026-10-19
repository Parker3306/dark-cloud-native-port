#include "dccore/motion_state.h"
#include "dccore/gamemath.h"

namespace dccore {

using namespace gm;

namespace {
constexpr float kMotionEps = 0.009999999776482582f;   // DAT_002a18bc
}

float step_motion_state(std::vector<MotionInfo>& table, MotionRequest& rq, MotionState& s, int& phase, PoseSample* sample) {
    phase = 0;
    if (rq.id < 0 || size_t(rq.id) >= table.size()) return -1.0f;
    MotionInfo& info = table[size_t(rq.id)];
    const float t0 = s.time;
    phase = 2;
    if (float(info.start) <= t0 && t0 <= float(info.end) && fsub(fsub(float(info.end), info.speed), kMotionEps) <= t0) phase = 3;
    s.cur = rq.id;
    if (s.cur != s.prev) {
        if (rq.id == 1 || rq.id == 2) {
            // phase matching: continue the new walk/run cycle at the same relative position
            const MotionInfo& np = table[size_t(s.cur)];
            const MotionInfo& op = table[size_t(s.prev)];
            int f = ftoi(fadd(float(np.start), fmul(fdiv(fsub(s.time, float(op.start)), float(op.end - op.start)), float(np.end - np.start))));
            // NextMotionTime_GET_EX(info, state): index [5] (new) is the target, [6] (previous) the source
            if (f > np.end) f = np.end;
            if (f < np.start) f = np.start;
            s.next = f + 2;
        } else {
            s.next = info.start;
            phase = 1;
        }
    }
    const float table_speed = info.speed;
    const float saved_step = s.blend_step;
    if (0.0f < rq.speed) info.speed = rq.speed;
    if ((rq.flags & 1) != 0) {
        info.speed = 0.0f;
        s.blend_step = 0.0f;
    }
    if ((rq.flags & 2) != 0 && float(info.end) <= fadd(fadd(kMotionEps, s.time), table_speed)) info.speed = 0.0f;
    if ((rq.flags & 4) != 0) {
        s.blend_step = 1.0f;
        s.frame = info.start;
        s.next = info.start;
        s.time = float(info.start);
    }
    rq.flags &= ~4u;
    // SetMotionEX (the part that moves the time; pose evaluation is the renderer's job)
    if (s.prev == s.cur && s.blending == 0) {
        s.frame = int(uint32_t(s.time));
        s.next = s.frame + 1;
        s.frac = fsub(s.time, float(uint32_t(s.frame)));
        if (sample) *sample = PoseSample{false, s.time, s.frame, s.next, s.frac};
        s.time = fadd(s.time, info.speed);
        const uint32_t whole = uint32_t(s.time);
        if (uint32_t(info.end) <= whole) {
            s.time = fsub(s.time, fsub(float(info.end), float(info.start)));
            s.frame = info.start;
            s.next = s.frame + 1;
            s.frac = fsub(s.time, float(s.frame));
        }
    } else {
        if (s.blending == 0) s.frac = 0.0f;
        s.blending = 1;
        s.frame = int(uint32_t(s.time));
        s.frac = fadd(s.frac, s.blend_step);
        s.prev = s.cur;
        if (1.0f <= s.frac) {
            s.time = float(uint32_t(s.next));
            s.frame = s.next;
            s.frac = 0.0f;
            s.blending = 0;
        }
        if (sample) *sample = PoseSample{true, s.time, s.frame, s.next, s.frac};
    }
    info.speed = table_speed;
    s.blend_step = saved_step;
    return t0;
}

}  // namespace dccore
