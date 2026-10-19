#pragma once
// Read-only access to the game files straight from the user's ISO image (ISO9660 root -> DATA.HED index -> DATA.DAT).
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

class Disc {
public:
    struct Entry {
        std::string name;  // as stored in DATA.HED (backslash separated)
        uint32_t offset;   // byte offset inside DATA.DAT
        uint32_t size;
    };

    explicit Disc(const std::filesystem::path& iso);

    const std::vector<Entry>& entries() const { return entries_; }
    bool contains(const std::string& name) const;
    const Entry& entry(const std::string& name) const;  // throws if missing
    Bytes read(const std::string& name) const;          // whole file
    Bytes read(const Entry& e) const;
    // A file from the ISO9660 root (outside DATA.DAT), e.g. "SCUS_971.11" (the ";1" version suffix is optional). Used for the main executable.
    Bytes read_root(const std::string& name) const;

    // Lower-case with forward slashes: the key used for lookups, so "DUN\\MainChara\\c01d.chr" == "dun/mainchara/c01d.chr".
    static std::string normalize(std::string name);

private:
    mutable std::ifstream f_;
    uint64_t dat_lba_ = 0;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
    struct RootFile { uint32_t lba, size; };
    std::unordered_map<std::string, RootFile> root_;
    Bytes read_raw(uint64_t pos, size_t n) const;
};

}  // namespace dccore
