#include "dccore/bytes.h"
#include <array>

namespace dccore {

static const std::array<uint32_t, 256>& table() {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            a[i] = c;
        }
        return a;
    }();
    return t;
}

uint32_t crc32(View data, uint32_t crc) {
    const auto& t = table();
    crc = ~crc;
    for (uint8_t b : data) crc = t[(crc ^ b) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

}  // namespace dccore
