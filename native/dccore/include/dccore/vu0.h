#pragma once
// The VU0 library helpers (sceVu0SubVector, InnerProduct, OuterProduct, Normalize, DistVector...) with the rounding of the real thing: every add, subtract, multiply, divide and
// square root truncates (measured in the running game: tools/dc/validate_vu.py, validate_vu2.py, validate_vu_axis.py).
#include <array>
#include <cstdlib>
#include "dccore/gamemath.h"

namespace dccore {
namespace vu0 {

using V3 = std::array<float, 3>;

inline constexpr bool native() { return false; }

inline V3 sub(const float* a, const float* b) { if (native()) return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; return {gm::vsub(a[0], b[0]), gm::vsub(a[1], b[1]), gm::vsub(a[2], b[2])}; }
inline V3 add(const float* a, const float* b) { if (native()) return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; return {gm::vadd(a[0], b[0]), gm::vadd(a[1], b[1]), gm::vadd(a[2], b[2])}; }
inline V3 scale(const float* a, float s) { if (native()) return {a[0] * s, a[1] * s, a[2] * s}; return {gm::vmul(a[0], s), gm::vmul(a[1], s), gm::vmul(a[2], s)}; }
inline float inner(const float* a, const float* b) { if (native()) return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; return gm::vadd(gm::vadd(gm::vmul(a[0], b[0]), gm::vmul(a[1], b[1])), gm::vmul(a[2], b[2])); }
// sceVu0OuterProduct(out, a, b) = a x b
inline V3 outer(const float* a, const float* b) {
    if (native()) return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    return {gm::vsub(gm::vmul(a[1], b[2]), gm::vmul(b[1], a[2])), gm::vsub(gm::vmul(a[2], b[0]), gm::vmul(b[2], a[0])), gm::vsub(gm::vmul(a[0], b[1]), gm::vmul(b[0], a[1]))};
}
// sceVu0Normalize (xyz): s = (x*x + y*y) + z*z, q = 1 / sqrt(s), v * q
inline V3 normalize(const float* v) {
    const float s = gm::vadd(gm::vadd(gm::vmul(v[0], v[0]), gm::vmul(v[1], v[1])), gm::vmul(v[2], v[2]));
    if (s == 0.0f) return {0.0f, 0.0f, 0.0f};
    const float q = gm::vdiv(1.0f, gm::vsqrt(s));
    return {gm::vmul(v[0], q), gm::vmul(v[1], q), gm::vmul(v[2], q)};
}
// DistVector(a, b): sqrt of the sum of squares of b - a
inline float dist(const float* a, const float* b) {
    const float dx = gm::vsub(b[0], a[0]), dy = gm::vsub(b[1], a[1]), dz = gm::vsub(b[2], a[2]);
    return gm::vsqrt(gm::vadd(gm::vadd(gm::vmul(dx, dx), gm::vmul(dy, dy)), gm::vmul(dz, dz)));
}

}  // namespace vu0
}  // namespace dccore
