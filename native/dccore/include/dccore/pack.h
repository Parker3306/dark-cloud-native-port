#pragma once
// Dark Cloud pack files (.chr .pak .pac .mpd): a list of named files. Entry = 0x50-byte header + data, see tools/dc/pack.py.
#include <string>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

struct PackEntry {
    std::string name;
    size_t offset;  // of the data inside the pack buffer
    size_t size;
};

class Pack {
public:
    explicit Pack(Bytes data);
    const std::vector<PackEntry>& entries() const { return entries_; }
    const PackEntry* find(const std::string& name) const;  // case-insensitive
    View data(const PackEntry& e) const { return View(buf_).subspan(e.offset, e.size); }
    View get(const std::string& name) const;                // throws if missing
private:
    Bytes buf_;
    std::vector<PackEntry> entries_;
};

}  // namespace dccore
