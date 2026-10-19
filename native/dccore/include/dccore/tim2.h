#pragma once
// TIM2 (PS2 texture) decoder and the .img container that holds named TIM2 textures. Port of scripts/tim2.py and tools/dc/img.py.
#include <string>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

struct Image {
    uint32_t width = 0, height = 0;
    Bytes rgba;  // width*height*4, row-major, top row first
};

class Tim2 {
public:
    // True if a valid TIM2 starts at `pos` in `data` (all sizes add up). Does not throw.
    static bool probe(View data, size_t pos);
    Tim2(View data, size_t pos);  // throws Error if not valid

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    int bpp() const { return bpp_; }
    // raw_alpha = false: alpha scaled to 0..255 (the PS2 uses 0x80 = opaque). true: alpha kept as stored.
    // swizzled: the 8-bit pixels are stored in GS page order (every texture of an "IM2" container is); they are put back into raster order first.
    Image decode(bool raw_alpha = false, bool swizzled = false) const;

private:
    View data_;
    size_t pos_ = 0, image_off_ = 0, clut_off_ = 0, clut_size_ = 0, image_size_ = 0;
    uint32_t width_ = 0, height_ = 0;
    uint8_t clut_type_ = 0;
    int bpp_ = 0;
    std::vector<uint8_t> palette(bool raw_alpha) const;  // 256 or 16 RGBA entries, linear order
};

struct ImgTexture {
    std::string name;
    size_t offset;  // of the TIM2 inside the .img buffer
    bool swizzled = false;  // IM2 containers store their 8-bit textures swizzled (PSMT8 page layout)
};

// 'IMG\0' container: u32 count at +4, 0x30-byte entries from +0x10 (name[0x20], u32 offset). Entries that are not valid TIM2 are skipped.
std::vector<ImgTexture> img_textures(View img);

}  // namespace dccore
