#include "dccore/elf.h"

namespace dccore {

Elf::Elf(Bytes data) : data_(std::move(data)) {
    if (data_.size() < 0x34 || data_[0] != 0x7f || data_[1] != 'E' || data_[2] != 'L' || data_[3] != 'F') throw Error("not an ELF file");
    const uint32_t phoff = rd<uint32_t>(data_, 28);
    const uint16_t phnum = rd<uint16_t>(data_, 44);
    for (uint16_t i = 0; i < phnum; ++i) {
        const size_t p = phoff + size_t(i) * 32;
        if (rd<uint32_t>(data_, p) != 1) continue;   // PT_LOAD
        segs_.push_back({rd<uint32_t>(data_, p + 8), rd<uint32_t>(data_, p + 4), rd<uint32_t>(data_, p + 16)});
    }
}

View Elf::at(uint32_t vaddr, size_t n) const {
    for (const Seg& s : segs_)
        if (vaddr >= s.vaddr && uint64_t(vaddr) + n <= uint64_t(s.vaddr) + s.filesz) return View(data_.data() + s.offset + (vaddr - s.vaddr), n);
    throw Error("address not in the executable");
}

std::string Elf::cstring(uint32_t vaddr, size_t max) const {
    std::string out;
    for (size_t i = 0; i < max; ++i) {
        const uint8_t c = at(vaddr + uint32_t(i), 1)[0];
        if (!c) break;
        out += char(c);
    }
    return out;
}

}  // namespace dccore
