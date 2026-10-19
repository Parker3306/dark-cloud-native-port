#include "dccore/pack.h"
#include <algorithm>
#include <cctype>

namespace dccore {

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Pack::Pack(Bytes data) : buf_(std::move(data)) {
    size_t p = 0;
    while (p + 0x50 <= buf_.size() && buf_[p] != 0) {
        uint32_t off = rd<uint32_t>(buf_, p + 0x40), size = rd<uint32_t>(buf_, p + 0x44), stride = rd<uint32_t>(buf_, p + 0x48);
        if (off < 0x50 || stride < uint64_t(off) + size || p + uint64_t(off) + size > buf_.size()) throw Error("bad pack entry");
        entries_.push_back({cstr(buf_, p, 0x40), p + off, size});
        if (stride == 0) break;
        p += stride;
    }
}

const PackEntry* Pack::find(const std::string& name) const {
    std::string k = lower(name);
    for (const auto& e : entries_)
        if (lower(e.name) == k) return &e;
    return nullptr;
}

View Pack::get(const std::string& name) const {
    const PackEntry* e = find(name);
    if (!e) throw Error("not in pack: " + name);
    return data(*e);
}

}  // namespace dccore
