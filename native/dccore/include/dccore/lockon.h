#pragma once
// Lock-on (the target cursor): which monster the player has locked on to and the point the attacks aim at. Shared between the walker (which turns the player,
// strafes, guards) and the monsters' world (which picks and validates the target every frame).
namespace dccore {

struct LockState {
    int target = -1;          // lockOnTargetNo: the monster in the cursor (-1 none)
    int flag = 0;             // lockOnTargetFlag: the player has locked on
    int shift_no = -1;        // targetCursorShiftNo
    int shift_rot = 0;        // targetCursorShiftRot: how many monsters are in reach
    float cursor_cnt = 0.0f;  // targetCursorCnt: grows while the target is out of reach; at 8 the lock is dropped
    float pos[4] = {0, 0, 0, 1};   // DAT_01dc4500: the point the attacks aim at
};

}  // namespace dccore
