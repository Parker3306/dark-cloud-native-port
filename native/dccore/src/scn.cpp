#include "dccore/scn.h"
#include <algorithm>
#include <cctype>

namespace dccore {

namespace {

constexpr size_t kScnHeader = 0x10, kScnSlot = 0x30, kScnSlots = 64, kFuncSize = 0xC0;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// header position of every model slot: tab entries (0..3) have name at 0x08+16i, offset at 0x48+4i, size at 0x58+4i; the others are {name16, off, size}
struct SlotPos {
    bool tab;
    size_t pos;
};
const SlotPos kSlotPos[kPtsSlots] = {{true, 0}, {true, 1}, {true, 2}, {true, 3}, {false, 0x80}, {false, 0x68}, {false, 0x98}, {false, 0xC8}, {false, 0xB0}};

Pts parse_pts(Bytes block, const std::string& name) {
    Pts p;
    p.name = name;
    p.block = std::move(block);
    const View b(p.block);
    if (b.size() < 0x100 || std::memcmp(b.data(), "PTS\0", 4) != 0) throw Error("scn: not a PTS block: " + name);
    const uint32_t io = rd<uint32_t>(b, 4);
    for (int s = 0; s < kPtsSlots; ++s) {
        const SlotPos sp = kSlotPos[s];
        size_t off, size;
        std::string nm;
        if (sp.tab) {
            nm = cstr(b, 8 + 16 * sp.pos, 16);
            off = rd<uint32_t>(b, 0x48 + 4 * sp.pos);
            size = rd<uint32_t>(b, 0x58 + 4 * sp.pos);
        } else {
            nm = cstr(b, sp.pos, 16);
            off = rd<uint32_t>(b, sp.pos + 16);
            size = rd<uint32_t>(b, sp.pos + 20);
        }
        if (size && (off + size > b.size() || off < io)) throw Error("scn: model slot out of range in " + name);
        p.models[s].name = size ? nm : "";
        p.models[s].offset = size ? off : 0;
        p.models[s].size = size;
    }
    // info block
    const View ib = b.subspan(io);
    PtsInfo& f = p.info;
    f.width = int(rd<uint32_t>(ib, 8));
    f.height = int(rd<uint32_t>(ib, 0x0C));
    f.max_count = int(rd<uint32_t>(ib, 0x10));
    f.f14 = int(rd<uint32_t>(ib, 0x14));
    f.f1c = int(rd<uint32_t>(ib, 0x1C));
    for (int k = 0; k < 3; ++k) { f.offset[k] = rd<float>(ib, 0x24 + 4 * size_t(k)); f.rotation[k] = rd<float>(ib, 0x30 + 4 * size_t(k)); }
    const size_t grid_off = rd<uint32_t>(ib, 0x3C);
    for (int k = 0; k < 6; ++k) f.element_ids[k] = rd<int32_t>(ib, 0x40 + 4 * size_t(k));
    const size_t func_off = rd<uint32_t>(ib, 0x70), func_count = rd<uint32_t>(ib, 0x74);
    const size_t cells = size_t(f.width) * size_t(f.height);
    View g = sub(ib, grid_off, cells);
    f.grid.assign(g.begin(), g.end());
    for (int k = 0; k < 6; ++k) {
        const size_t so = rd<uint32_t>(ib, 0x58 + 4 * size_t(k));
        if (so) f.elements[k] = cstr(ib, so, std::min<size_t>(64, ib.size() - so));
    }
    for (size_t i = 0; i < func_count; ++i) {
        const View r = sub(ib, func_off + i * kFuncSize, kFuncSize);
        PtsFunc fn;
        fn.label = cstr(r, 0, 16);
        fn.type = rd<int32_t>(r, 0x10);
        fn.t_start = rd<float>(r, 0x18);
        fn.t_end = rd<float>(r, 0x1C);
        fn.id = rd<int32_t>(r, 0x20);
        fn.flag = rd<int32_t>(r, 0x24);
        fn.enabled = rd<int32_t>(r, 0x28);
        fn.target = cstr(r, 0x30, 16);
        for (int k = 0; k < 3; ++k) { fn.pos[k] = rd<float>(r, 0x40 + 4 * size_t(k)); fn.rot[k] = rd<float>(r, 0x50 + 4 * size_t(k)); fn.scale[k] = rd<float>(r, 0x60 + 4 * size_t(k)); }
        for (int k = 0; k < 4; ++k) fn.params[k] = rd<float>(r, 0x70 + 4 * size_t(k));
        for (int k = 0; k < 16; ++k) fn.matrix[k] = rd<float>(r, 0x80 + 4 * size_t(k));
        f.funcs.push_back(std::move(fn));
    }
    return p;
}

}  // namespace

Scn parse_scn(View data) {
    if (data.size() < kScnHeader || std::memcmp(data.data(), "SCN\0", 4) != 0) throw Error("scn: not an SCN container");
    const uint32_t count = rd<uint32_t>(data, 4);
    if (count > kScnSlots) throw Error("scn: too many entries");
    Scn scn;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t p = kScnHeader + i * kScnSlot;
        const std::string name = cstr(data, p, 16);
        const size_t off = rd<uint32_t>(data, p + 16), size = rd<uint32_t>(data, p + 20);
        View blk = sub(data, off, size);
        scn.parts.push_back(parse_pts(Bytes(blk.begin(), blk.end()), name));
    }
    return scn;
}

const Pts* Scn::find(const std::string& name) const {
    std::string key = name;
    const size_t slash = key.find_last_of("/\\");
    if (slash != std::string::npos) key = key.substr(slash + 1);
    for (const char* ext : {".pts", ".PTS"}) {
        if (key.size() > 4 && key.compare(key.size() - 4, 4, ext) == 0) key.resize(key.size() - 4);
    }
    for (const Pts& p : parts)
        if (p.name == key) return &p;
    const std::string lk = lower(key);
    for (const Pts& p : parts)
        if (lower(p.name) == lk) return &p;
    return nullptr;
}

std::string frame_key(const std::string& name) {
    const size_t i = name.find("__");
    return i == std::string::npos ? name : name.substr(0, i);
}

}  // namespace dccore
