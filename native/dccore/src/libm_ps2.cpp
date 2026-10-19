// fdlibm float versions of sin/cos/atan/atan2 as compiled into the game (functions at 0x11d8a0, 0x11d6b0, 0x11d408, 0x11a5e8), every operation truncating.
#include "dccore/libm_ps2.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include "dccore/gamemath.h"

namespace dccore {

namespace {

using namespace gm;

uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float from_bits(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

constexpr uint32_t kNpio2Hw[32] = {0x3fc90f00, 0x40490f00, 0x4096cb00, 0x40c90f00, 0x40fb5300, 0x4116cb00, 0x412fed00, 0x41490f00, 0x41623100, 0x417b5300, 0x418a3a00, 0x4196cb00, 0x41a35c00,
                                   0x41afed00, 0x41bc7e00, 0x41c90f00, 0x41d5a000, 0x41e23100, 0x41eec200, 0x41fb5300, 0x4203f200, 0x420a3a00, 0x42108300, 0x4216cb00, 0x421d1400, 0x42235c00,
                                   0x4229a500, 0x422fed00, 0x42363600, 0x423c7e00, 0x4242c700, 0x42490f00};

float kernel_sin(float x, float y, bool has_y) {
    if ((bits(x) & 0x7fffffffu) < 0x32000000u && int32_t(x) == 0) return x;
    const float z = fmul(x, x);
    const float v = fmul(z, x);
    const float r = fadd(fmul(z, fadd(fmul(z, fadd(fmul(z, fadd(fmul(z, 1.589691e-10f), -2.505076e-08f)), 2.7557314e-06f)), -0.0001984127f)), 0.008333334f);
    if (has_y) return fsub(x, fsub(fsub(fmul(z, fsub(fmul(y, 0.5f), fmul(v, r))), y), fmul(v, -0.16666667f)));
    return fadd(x, fmul(v, fadd(fmul(z, r), -0.16666667f)));
}

float kernel_cos(float x, float y) {
    const uint32_t ix = bits(x) & 0x7fffffffu;
    if (ix < 0x32000000u && int32_t(x) == 0) return 1.0f;
    const float z = fmul(x, x);
    const float r = fmul(z, fadd(fmul(z, fadd(fmul(z, fadd(fmul(z, fadd(fmul(z, fadd(fmul(z, -1.1359648e-11f), 2.0875723e-09f)), -2.7557314e-07f)), 2.4801588e-05f)), -0.0013888889f)), 0.041666668f));
    if (0x3e999999u < ix) {
        float qx;
        if (ix < 0x3f480001u) qx = from_bits(ix - 0x1000000u);
        else qx = 0.28125f;
        return fsub(fsub(1.0f, qx), fsub(fsub(fmul(z, 0.5f), qx), fsub(fmul(z, r), fmul(x, y))));
    }
    return fsub(1.0f, fsub(fmul(z, 0.5f), fsub(fmul(z, r), fmul(x, y))));
}

// __ieee754_rem_pio2f for |x| below about 201: x = n * pi/2 + y[0] + y[1]. Larger arguments are not used by the game's code.
int rem_pio2(float x, float y[2]) {
    const uint32_t hx = bits(x), ix = hx & 0x7fffffffu;
    if (ix < 0x3f490fd9u) { y[0] = x; y[1] = 0.0f; return 0; }
    if (ix < 0x4016cbe4u) {
        if (int32_t(hx) > 0) {
            float z = fsub(x, 1.5707855f), w, r;
            if ((hx & 0x7ffffff0u) == 0x3fc90fd0u) {
                w = 6.0771e-11f;
                z = fsub(z, 1.0804273e-05f);
                r = fsub(z, 6.0771e-11f);
            } else {
                w = 1.0804334e-05f;
                r = fsub(z, 1.0804334e-05f);
            }
            y[0] = r;
            y[1] = fsub(fsub(z, r), w);
            return 1;
        }
        float z = fadd(x, 1.5707855f), w, r;
        if ((hx & 0x7ffffff0u) == 0x3fc90fd0u) {
            w = 6.0771e-11f;
            z = fadd(z, 1.0804273e-05f);
            r = fadd(z, 6.0771e-11f);
        } else {
            w = 1.0804334e-05f;
            r = fadd(z, 1.0804334e-05f);
        }
        y[0] = r;
        y[1] = fadd(fsub(z, r), w);
        return -1;
    }
    if (ix > 0x43490f80u) {   // huge: not needed by the game's trigonometry
        y[0] = float(std::fmod(double(x), 1.5707963267948966));
        y[1] = 0.0f;
        return 0;
    }
    const int e = int(ix >> 23);
    const float ax = from_bits(ix);
    const int n = ftoi(fadd(fmul(ax, 0.6366198f), 0.5f));
    const float t = itof(n);
    float w = fmul(t, 1.0804334e-05f);
    float r = fsub(ax, fmul(t, 1.5707855f));
    float y0, rr;
    if (n < 0x20 && (hx & 0x7fffff00u) != kNpio2Hw[n - 1]) {
        y0 = fsub(r, w);
        rr = r;
    } else {
        y0 = fsub(r, w);
        const int j = int((bits(y0) >> 23) & 0xff);
        if (e - j < 9) {
            rr = r;
        } else {
            const float r2 = fsub(r, fmul(t, 1.0804273e-05f));
            w = fsub(fmul(t, 6.0771e-11f), fsub(fsub(r, r2), fmul(t, 1.0804273e-05f)));
            y0 = fsub(r2, w);
            const int j2 = int((bits(fsub(r2, w)) >> 23) & 0xff);
            if (0x19 < e - j2) {
                const float r3 = fsub(r2, fmul(t, 6.0770944e-11f));
                w = fsub(fmul(t, 6.123234e-17f), fsub(fsub(r2, r3), fmul(t, 6.0770944e-11f)));
                y0 = fsub(r3, w);
                rr = r3;
            } else {
                rr = r2;
            }
        }
    }
    float y1 = fsub(fsub(rr, y0), w);
    if (int32_t(hx) >= 0) {
        y[0] = y0;
        y[1] = y1;
        return n;
    }
    y[0] = -y0;
    y[1] = -y1;
    return -n;
}

constexpr float kAtanHi[4] = {4.6364760399e-01f, 7.8539812565e-01f, 9.8279368877e-01f, 1.5707962513e+00f};
constexpr float kAtanLo[4] = {5.0121582440e-09f, 3.7748947079e-08f, 3.4473217170e-08f, 7.5497894159e-08f};
constexpr float kAT[11] = {3.3333334327e-01f, -2.0000000298e-01f, 1.4285714924e-01f, -1.1111110449e-01f, 9.0908870101e-02f, -7.6918758452e-02f,
                           6.6610731184e-02f, -5.8335702866e-02f, 4.9768779427e-02f, -3.6531571299e-02f, 1.6285819933e-02f};

}  // namespace

float ps2_sinf(float x) {
    const uint32_t ix = bits(x) & 0x7fffffffu;
    if (ix < 0x3f490fd9u) return kernel_sin(x, 0.0f, false);
    if (ix >= 0x7f800000u) return fsub(x, x);
    float y[2];
    const int n = rem_pio2(x, y) & 3;
    switch (n) {
        case 0: return kernel_sin(y[0], y[1], true);
        case 1: return kernel_cos(y[0], y[1]);
        case 2: return -kernel_sin(y[0], y[1], true);
        default: return -kernel_cos(y[0], y[1]);
    }
}

float ps2_cosf(float x) {
    const uint32_t ix = bits(x) & 0x7fffffffu;
    if (ix < 0x3f490fd9u) return kernel_cos(x, 0.0f);
    if (ix >= 0x7f800000u) return fsub(x, x);
    float y[2];
    const int n = rem_pio2(x, y) & 3;
    switch (n) {
        case 0: return kernel_cos(y[0], y[1]);
        case 1: return -kernel_sin(y[0], y[1], true);
        case 2: return -kernel_cos(y[0], y[1]);
        default: return kernel_sin(y[0], y[1], true);
    }
}

float ps2_atanf(float x) {
    const uint32_t hx = bits(x), ix = hx & 0x7fffffffu;
    if (ix >= 0x50800000u) {
        if (ix > 0x7f800000u) return fadd(x, x);
        if (int32_t(hx) > 0) return fadd(kAtanHi[3], kAtanLo[3]);
        return fsub(-kAtanHi[3], kAtanLo[3]);
    }
    int id;
    float v = x;
    if (ix < 0x3ee00000u) {
        id = -1;
        if (ix < 0x31000000u) {
            if (1.0f < fadd(x, 1e+30f)) return x;
        }
    } else {
        v = from_bits(ix);
        if (ix < 0x3f980000u) {
            if (ix < 0x3f300000u) {
                id = 0;
                v = fdiv(fsub(fadd(v, v), 1.0f), fadd(v, 2.0f));
            } else {
                id = 1;
                v = fdiv(fsub(v, 1.0f), fadd(v, 1.0f));
            }
        } else if (ix < 0x401c0000u) {
            id = 2;
            v = fdiv(fsub(v, 1.5f), fadd(fmul(v, 1.5f), 1.0f));
        } else {
            id = 3;
            v = fdiv(-1.0f, v);
        }
    }
    const float z = fmul(v, v);
    const float w = fmul(z, z);
    const float s2 = fmul(w, fadd(kAT[1], fmul(w, fadd(kAT[3], fmul(w, fadd(kAT[5], fmul(w, fadd(kAT[7], fmul(w, kAT[9])))))))));
    const float s1 = fmul(z, fadd(kAT[0], fmul(w, fadd(kAT[2], fmul(w, fadd(kAT[4], fmul(w, fadd(kAT[6], fmul(w, fadd(kAT[8], fmul(w, kAT[10])))))))))));
    if (id < 0) return fsub(v, fmul(v, fadd(s1, s2)));
    float r = fsub(kAtanHi[id], fsub(fsub(fmul(v, fadd(s1, s2)), kAtanLo[id]), v));
    if (int32_t(hx) < 0) r = -r;
    return r;
}

float ps2_atan2f(float y, float x) {
    const uint32_t hy = bits(y), hx = bits(x), iy = hy & 0x7fffffffu, ix = hx & 0x7fffffffu;
    if (ix > 0x7f800000u || iy > 0x7f800000u) return fadd(x, y);
    if (hx == 0x3f800000u) return ps2_atanf(y);
    const uint32_t m = (hy >> 31) | ((hx >> 30) & 2u);
    constexpr float pi = 3.1415925f, pi_lo = 1.5099579e-07f;
    if (iy == 0) {
        switch (m) {
            case 0: case 1: return y;
            case 2: return pi;
            default: return -pi;
        }
    }
    if (ix == 0) return int32_t(hy) < 0 ? -1.5707964f : 1.5707964f;
    if (ix == 0x7f800000u || iy == 0x7f800000u) return std::atan2(y, x);   // infinities: not needed
    const int k = (int(iy) - int(ix)) >> 23;
    float z;
    if (k > 0x3c) z = 1.5707964f;
    else if (int32_t(hx) < 0 && k < -0x3c) z = 0.0f;
    else z = ps2_atanf(from_bits(bits(fdiv(y, x)) & 0x7fffffffu));
    switch (m) {
        case 0: return z;
        case 1: return -z;
        case 2: return fsub(pi, fsub(z, pi_lo));
        default: return fsub(fsub(z, pi_lo), pi);
    }
}

}  // namespace dccore
