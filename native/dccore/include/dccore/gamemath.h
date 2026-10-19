#pragma once
// Number crunching the way the PS2 game does it. The EE's FPU rounds toward zero, so every float operation here is computed exactly in double and truncated
// toward zero to a float (see dungeon.cpp's history: this was needed to reproduce the game's random picks). The game's random numbers come from the libc
// rand() (`GameRand`, dungeon.h).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dccore {

namespace gm {

// Set false only to experiment with round-to-nearest.
inline bool& round_to_zero() {
    static bool v = true;
    return v;
}

inline float to_float(double d) {
    float f = static_cast<float>(d);
    if (round_to_zero() && std::fabs(static_cast<double>(f)) > std::fabs(d)) f = std::nextafter(f, 0.0f);
    return f;
}
inline float itof(int32_t v) { return to_float(static_cast<double>(v)); }
inline float fmul(float a, float b) { return to_float(static_cast<double>(a) * static_cast<double>(b)); }
// The EE adder (measured on the running game, tools/dc/validate_fpu.py, 6000 random operands each, no exceptions): the smaller operand is aligned to the larger
// one with ONE guard bit (bits below 2^(e-24) are cut off), the aligned values are added or subtracted exactly and the result is truncated to 24 bits.
// For an effective subtraction this differs from a correctly truncated exact result. Multiplication truncates exactly; division rounds to nearest.
inline float ee_add(float a, float b) {
    if (a == 0.0f) return b;
    if (b == 0.0f) return a;
    double big = a, small = b;
    if (std::fabs(big) < std::fabs(small)) std::swap(big, small);
    const int e = std::ilogb(big);
    const double grid = std::ldexp(1.0, e - 24);
    const double cut = std::copysign(std::floor(std::fabs(small) / grid) * grid, small);
    return to_float(big + cut);
}
inline float fadd(float a, float b) { return round_to_zero() ? ee_add(a, b) : static_cast<float>(static_cast<double>(a) + static_cast<double>(b)); }
inline float fsub(float a, float b) { return round_to_zero() ? ee_add(a, -b) : static_cast<float>(static_cast<double>(a) - static_cast<double>(b)); }
inline float fdiv(float a, float b) { return static_cast<float>(static_cast<double>(a) / static_cast<double>(b)); }
// VU0 macro code (sceVu0* library functions): add, subtract, multiply, divide and square root all truncate the exact result (measured the same way, 2000 vectors each).
inline float vadd(float a, float b) { return to_float(static_cast<double>(a) + static_cast<double>(b)); }
inline float vsub(float a, float b) { return to_float(static_cast<double>(a) - static_cast<double>(b)); }
inline float vmul(float a, float b) { return to_float(static_cast<double>(a) * static_cast<double>(b)); }
inline float vdiv(float a, float b) { return to_float(static_cast<double>(a) / static_cast<double>(b)); }
inline float vsqrt(float a) { return to_float(std::sqrt(static_cast<double>(a))); }
// EE FPU square root rounds to nearest
inline float fsqrt(float a) { return static_cast<float>(std::sqrt(static_cast<double>(a))); }
inline int32_t ftoi(float f) { return static_cast<int32_t>(f); }

constexpr float kRandRange = 2147483648.0f;   // the literal 2.1474836e+09 in the game's code is exactly 2^31 as a float

// (float)scale * (float)r / 2.1474836e9, truncated: the game's way of mapping a rand() onto 0 .. scale
inline int32_t scaled(int32_t scale, int32_t r) { return ftoi(fdiv(fmul(itof(scale), itof(r)), kRandRange)); }
// the same without truncation: a float in 0 .. scale
inline float scaledf(float scale, int32_t r) { return fdiv(fmul(scale, itof(r)), kRandRange); }
// percent test used by the generators: (float)r * 100.0 / 2.1474836e9 <= limit
inline bool percent_le(int32_t r, float limit) { return fdiv(fmul(itof(r), 100.0f), kRandRange) <= limit; }

}  // namespace gm

}  // namespace dccore
