// dclibm <sin|cos|atan|atan2>: reads lines of hex float bit patterns (one for sin/cos/atan, two "y x" for atan2) and prints the bit pattern of the game's result.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include "dccore/gamemath.h"
#include "dccore/libm_ps2.h"

static float from_bits(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    if (std::getenv("DC_RN")) dccore::gm::round_to_zero() = false;
    const std::string fn = argv[1];
    unsigned a, b;
    char line[128];
    while (std::fgets(line, sizeof line, stdin)) {
        const int n = std::sscanf(line, "%x %x", &a, &b);
        if (n < 1) continue;
        float r = 0;
        if (fn == "sin") r = dccore::ps2_sinf(from_bits(a));
        else if (fn == "cos") r = dccore::ps2_cosf(from_bits(a));
        else if (fn == "atan") r = dccore::ps2_atanf(from_bits(a));
        else if (fn == "atan2" && n == 2) r = dccore::ps2_atan2f(from_bits(a), from_bits(b));
        std::printf("%08x\n", bits(r));
    }
    return 0;
}
