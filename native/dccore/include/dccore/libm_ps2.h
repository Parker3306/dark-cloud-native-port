#pragma once
// The game's own single-precision libm (newlib / fdlibm float versions): sinf, cosf, atanf, atan2f, run on the EE FPU, which truncates every operation.
// The results differ from a normal libm in the last bit, which matters because game code adds them to numbers around 1000-2000 (positions) and the values then
// steer scripts; the lock-step comparisons with the real game need them to be identical.
namespace dccore {

float ps2_sinf(float x);
float ps2_cosf(float x);
float ps2_atanf(float x);
float ps2_atan2f(float y, float x);

}  // namespace dccore
