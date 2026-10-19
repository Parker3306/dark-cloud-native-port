#include "dccore/tim2.h"
#include <algorithm>

namespace dccore {

namespace {
// image_type & 0x3F -> bits per pixel (0 = unsupported)
int bpp_of(uint8_t t) {
    switch (t & 0x3F) {
        case 1: return 16;
        case 2: return 24;
        case 3: return 32;
        case 4: return 4;
        case 5: return 8;
        default: return 0;
    }
}

struct Header {
    uint32_t total, clut_size, image_size;
    uint16_t header_size, clut_colors;
    uint8_t pict_fmt, mipmaps, clut_type, image_type;
    uint16_t width, height;
    size_t hdr;
};

bool read_header(View d, size_t pos, Header& h) {
    if (pos + 16 > d.size() || std::memcmp(d.data() + pos, "TIM2", 4) != 0) return false;
    uint8_t fmt = d[pos + 5];
    size_t hdr = pos + (fmt == 0 ? 0x10 : 0x80);
    if (hdr + 48 > d.size()) return false;
    h.total = rd<uint32_t>(d, hdr);
    h.clut_size = rd<uint32_t>(d, hdr + 4);
    h.image_size = rd<uint32_t>(d, hdr + 8);
    h.header_size = rd<uint16_t>(d, hdr + 12);
    h.clut_colors = rd<uint16_t>(d, hdr + 14);
    h.pict_fmt = d[hdr + 16];
    h.mipmaps = d[hdr + 17];
    h.clut_type = d[hdr + 18];
    h.image_type = d[hdr + 19];
    h.width = rd<uint16_t>(d, hdr + 20);
    h.height = rd<uint16_t>(d, hdr + 22);
    h.hdr = hdr;
    return true;
}

bool valid(View d, const Header& h) {
    int bpp = bpp_of(h.image_type);
    if (bpp == 0 || h.width < 1 || h.width > 1024 || h.height < 1 || h.height > 1024) return false;
    uint64_t base = uint64_t(h.width) * h.height * bpp / 8;
    uint64_t image_off = h.hdr + h.header_size;
    return base <= h.image_size && h.header_size >= 48 && image_off + h.image_size + h.clut_size <= d.size() &&
           h.total >= int64_t(h.header_size) + h.image_size + h.clut_size - 16;
}

inline void put16(uint8_t* o, uint16_t v) {  // 15-bit BGR1555 as stored by the PS2 (r in the low bits), alpha = top bit
    o[0] = uint8_t((v & 31) * 255 / 31);
    o[1] = uint8_t(((v >> 5) & 31) * 255 / 31);
    o[2] = uint8_t(((v >> 10) & 31) * 255 / 31);
    o[3] = (v >> 15) ? 255 : 0;
}
}  // namespace

bool Tim2::probe(View data, size_t pos) {
    Header h;
    return read_header(data, pos, h) && valid(data, h);
}

Tim2::Tim2(View data, size_t pos) : data_(data), pos_(pos) {
    Header h;
    if (!read_header(data, pos, h) || !valid(data, h)) throw Error("not a valid TIM2");
    width_ = h.width;
    height_ = h.height;
    bpp_ = bpp_of(h.image_type);
    clut_type_ = h.clut_type;
    image_size_ = h.image_size;
    clut_size_ = h.clut_size;
    image_off_ = h.hdr + h.header_size;
    clut_off_ = image_off_ + image_size_;
}

std::vector<uint8_t> Tim2::palette(bool raw_alpha) const {
    const int ncol = (bpp_ == 8) ? 256 : 16;
    std::vector<uint8_t> pal(size_t(ncol) * 4, 0);
    View raw = sub(data_, clut_off_, clut_size_);
    int ctype = clut_type_ & 0x3F;
    if (ctype == 3) {
        for (int i = 0; i < ncol; ++i) {
            for (int c = 0; c < 4; ++c) pal[i * 4 + c] = rd<uint8_t>(raw, size_t(i) * 4 + c);
            if (!raw_alpha) pal[i * 4 + 3] = uint8_t(std::min<int>(pal[i * 4 + 3] * 2, 255));
        }
    } else if (ctype == 2) {
        for (int i = 0; i < ncol; ++i) {
            for (int c = 0; c < 3; ++c) pal[i * 4 + c] = rd<uint8_t>(raw, size_t(i) * 3 + c);
            pal[i * 4 + 3] = 255;
        }
    } else {
        for (int i = 0; i < ncol; ++i) put16(&pal[i * 4], rd<uint16_t>(raw, size_t(i) * 2));
    }
    // 8-bit CLUTs stored for the GS are "swizzled": entries 8..15 and 16..23 of every 32 swap places. Bit 7 of clut_type = already linear.
    if (bpp_ == 8 && !(clut_type_ & 0x80)) {
        std::vector<uint8_t> out(pal.size());
        for (int i = 0; i < 256; ++i) {
            int blk = (i / 8) % 4;
            int src = blk == 1 ? i + 8 : (blk == 2 ? i - 8 : i);
            std::memcpy(&out[i * 4], &pal[src * 4], 4);
        }
        pal.swap(out);
    }
    return pal;
}

namespace {
// PSMT8 page/block/column layout -> raster order (the widely used unswizzle for 8-bit PS2 textures). Dimensions are multiples of 16 in practice; other
// sizes fall back to the data as it is.
std::vector<uint8_t> unswizzle8(View raw, uint32_t w, uint32_t h) {
    std::vector<uint8_t> out(size_t(w) * h);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t block = (y & ~0xfu) * w + (x & ~0xfu) * 2;
            const uint32_t swap = (((y + 2) >> 2) & 1u) * 4;
            const uint32_t pos_y = (((y & ~3u) >> 1) + (y & 1u)) & 7u;
            const uint32_t column = pos_y * w * 2 + ((x + swap) & 7u) * 4;
            const uint32_t byte = ((y >> 1) & 1u) + ((x >> 2) & 2u);
            const size_t src = size_t(block) + column + byte;
            out[size_t(y) * w + x] = src < raw.size() ? raw[src] : 0;
        }
    return out;
}
}  // namespace

Image Tim2::decode(bool raw_alpha, bool swizzled) const {
    Image im;
    im.width = width_;
    im.height = height_;
    const size_t n = size_t(width_) * height_;
    im.rgba.resize(n * 4);
    const size_t bytes = n * bpp_ / 8;
    View raw = sub(data_, image_off_, bytes);
    if (bpp_ == 32) {
        for (size_t i = 0; i < n; ++i) {
            for (int c = 0; c < 3; ++c) im.rgba[i * 4 + c] = raw[i * 4 + c];
            uint8_t a = raw[i * 4 + 3];
            im.rgba[i * 4 + 3] = raw_alpha ? a : uint8_t(std::min<int>(a * 2, 255));
        }
    } else if (bpp_ == 24) {
        for (size_t i = 0; i < n; ++i) {
            for (int c = 0; c < 3; ++c) im.rgba[i * 4 + c] = raw[i * 3 + c];
            im.rgba[i * 4 + 3] = 255;
        }
    } else if (bpp_ == 16) {
        for (size_t i = 0; i < n; ++i) put16(&im.rgba[i * 4], rd<uint16_t>(raw, i * 2));
    } else {
        std::vector<uint8_t> pal = palette(raw_alpha);
        std::vector<uint8_t> raster;
        if (bpp_ == 8 && swizzled && width_ % 16 == 0 && height_ % 16 == 0) raster = unswizzle8(raw, width_, height_);
        for (size_t i = 0; i < n; ++i) {
            size_t idx = (bpp_ == 8) ? (raster.empty() ? raw[i] : raster[i]) : ((i & 1) ? (raw[i / 2] >> 4) : (raw[i / 2] & 15));  // 4-bit: low nibble is the first pixel
            std::memcpy(&im.rgba[i * 4], &pal[idx * 4], 4);
        }
    }
    return im;
}

std::vector<ImgTexture> img_textures(View img) {
    std::vector<ImgTexture> out;
    // "IM2\0" is the same layout as "IMG\0" and is used by about two thirds of the game's containers.
    if (img.size() < 16 || (std::memcmp(img.data(), "IMG\0", 4) != 0 && std::memcmp(img.data(), "IM2\0", 4) != 0)) throw Error("not an IMG container");
    const bool swizzled = std::memcmp(img.data(), "IM2\0", 4) == 0;
    uint32_t count = rd<uint32_t>(img, 4);
    for (uint32_t i = 0; i < count; ++i) {
        size_t base = 0x10 + 0x30 * size_t(i);
        std::string name = cstr(img, base, 0x20);
        uint32_t off = rd<uint32_t>(img, base + 0x20);
        if (Tim2::probe(img, off)) out.push_back({std::move(name), off, swizzled});
    }
    return out;
}

}  // namespace dccore
