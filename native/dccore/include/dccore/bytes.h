#pragma once
// Small byte-level helpers shared by all readers. dccore assumes a little-endian host (the game data is little-endian).
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dccore {

using Bytes = std::vector<uint8_t>;
using View = std::span<const uint8_t>;

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Bounds-checked read of a trivially-copyable value at a byte offset.
template <class T>
inline T rd(View b, size_t off) {
    if (off > b.size() || sizeof(T) > b.size() - off) throw Error("read past end of buffer");
    T v;
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

inline View sub(View b, size_t off, size_t n) {
    if (off > b.size() || n > b.size() - off) throw Error("sub-view past end of buffer");
    return b.subspan(off, n);
}

// NUL-terminated string stored in a fixed-size field (stops at the first NUL).
inline std::string cstr(View b, size_t off, size_t max) {
    View f = sub(b, off, max);
    size_t n = 0;
    while (n < f.size() && f[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(f.data()), n);
}

uint32_t crc32(View data, uint32_t crc = 0);  // same polynomial and convention as Python's zlib.crc32

}  // namespace dccore
