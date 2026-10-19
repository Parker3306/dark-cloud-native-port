#pragma once
// Minimal ELF32 reader for the game's main executable (SCUS_971.11): the data tables of the game (monster database, enemy layouts, item-placement areas,
// script function ids...) are read from the player's own disc at start-up by virtual address, so nothing of it has to be copied into this project.
#include <cstdint>
#include <string>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

class Elf {
public:
    explicit Elf(Bytes data);
    // Bytes at a virtual address (throws if outside the loadable segments).
    View at(uint32_t vaddr, size_t n) const;
    template <class T>
    T get(uint32_t vaddr) const {
        T v;
        std::memcpy(&v, at(vaddr, sizeof(T)).data(), sizeof(T));
        return v;
    }
    std::string cstring(uint32_t vaddr, size_t max = 256) const;

private:
    struct Seg { uint32_t vaddr, offset, filesz; };
    Bytes data_;
    std::vector<Seg> segs_;
};

}  // namespace dccore
