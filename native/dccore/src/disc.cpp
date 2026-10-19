#include "dccore/disc.h"
#include <algorithm>
#include <cctype>

namespace dccore {

static constexpr uint64_t kSector = 2048;

std::string Disc::normalize(std::string s) {
    for (char& c : s) c = (c == '\\') ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Bytes Disc::read_raw(uint64_t pos, size_t n) const {
    Bytes b(n);
    f_.clear();
    f_.seekg(static_cast<std::streamoff>(pos));
    f_.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(n));
    if (static_cast<size_t>(f_.gcount()) != n) throw Error("short read from ISO");
    return b;
}

Disc::Disc(const std::filesystem::path& iso) : f_(iso, std::ios::binary) {
    if (!f_) throw Error("cannot open ISO: " + iso.string());
    Bytes pvd = read_raw(16 * kSector, kSector);
    if (std::memcmp(pvd.data() + 1, "CD001", 5) != 0) throw Error("not an ISO9660 image");
    uint32_t root_lba = rd<uint32_t>(pvd, 158), root_len = rd<uint32_t>(pvd, 166);
    Bytes dir = read_raw(root_lba * kSector, root_len);

    struct F { uint32_t lba, size; };
    std::unordered_map<std::string, F> files;
    for (size_t pos = 0; pos < dir.size();) {
        uint8_t n = dir[pos];
        if (n == 0) { pos = (pos / kSector + 1) * kSector; continue; }
        std::string name = cstr(dir, pos + 33, dir[pos + 32]);
        files[name] = {rd<uint32_t>(dir, pos + 2), rd<uint32_t>(dir, pos + 10)};
        pos += n;
    }
    for (const char* need : {"DATA.HED;1", "DATA.DAT;1"})
        if (!files.count(need)) throw Error(std::string(need) + " not found in the ISO root");
    for (auto& [k, v] : files) root_[k] = {v.lba, v.size};
    dat_lba_ = files["DATA.DAT;1"].lba;
    Bytes hed = read_raw(uint64_t(files["DATA.HED;1"].lba) * kSector, files["DATA.HED;1"].size);
    for (size_t i = 0; i + 80 <= hed.size(); i += 80) {
        Entry e{cstr(hed, i, 64), rd<uint32_t>(hed, i + 64), rd<uint32_t>(hed, i + 68)};
        // Empty-named entries are kept so that entry indices match DATA.HED; they are just not reachable by name.
        if (!e.name.empty()) index_[normalize(e.name)] = entries_.size();  // names can repeat (only titledat/lang_set.img does); the last copy wins, like tools/dc/disc.py
        entries_.push_back(std::move(e));
    }
}

bool Disc::contains(const std::string& name) const { return index_.count(normalize(name)) != 0; }

const Disc::Entry& Disc::entry(const std::string& name) const {
    auto it = index_.find(normalize(name));
    if (it == index_.end()) throw Error("file not on disc: " + name);
    return entries_[it->second];
}

Bytes Disc::read_root(const std::string& name) const {
    auto it = root_.find(name);
    if (it == root_.end()) it = root_.find(name + ";1");
    if (it == root_.end()) throw Error("file not in the ISO root: " + name);
    return read_raw(uint64_t(it->second.lba) * kSector, it->second.size);
}

Bytes Disc::read(const Entry& e) const { return read_raw(dat_lba_ * kSector + e.offset, e.size); }
Bytes Disc::read(const std::string& name) const { return read(entry(name)); }

}  // namespace dccore
