// Prints the same digest lines as native/tools/make_golden.py, computed with the C++ readers in dccore. See that file for the line formats.
// Usage: dcgolden <iso> <out.txt> [--limit-files N]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "dccore/disc.h"
#include "dccore/mds.h"
#include "dccore/pack.h"
#include "dccore/tim2.h"
#include "dccore/wgt.h"

using namespace dccore;

static std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

static std::string utf8(const std::string& latin1) {  // Python wrote these names as UTF-8 after decoding them as latin-1
    std::string o;
    for (unsigned char c : latin1) {
        if (c < 0x80) o += char(c);
        else { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
    }
    return o;
}

static bool ends_with(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

static std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

template <class T>
static View raw(const std::vector<T>& v) {
    return View(reinterpret_cast<const uint8_t*>(v.data()), v.size() * sizeof(T));
}

static void mds_lines(std::vector<std::string>& out, const std::string& label, View data) {
    Mds m;
    try { m = parse_mds(data); } catch (const std::exception&) { out.push_back("M " + label + " ERR"); return; }
    uint32_t c = 0;
    for (const auto& o : m.objects) {
        Bytes nm(o.name.begin(), o.name.end());
        nm.push_back(0);
        c = crc32(nm, c);
        c = crc32(View(reinterpret_cast<const uint8_t*>(&o.parent), 4), c);
        c = crc32(View(reinterpret_cast<const uint8_t*>(o.matrix.data()), 64), c);
    }
    out.push_back("M " + label + " " + std::to_string(m.objects.size()) + " " + hex8(c));
    for (size_t i = 0; i < m.objects.size(); ++i) {
        const auto& o = m.objects[i];
        if (!o.has_mesh) continue;
        const Mesh& me = o.mesh;
        uint32_t cg = 0;
        for (const auto& g : me.groups) {
            int32_t hdr[3] = {g.type, int32_t(g.tuples.size() / size_t(me.stride)), g.material};
            cg = crc32(View(reinterpret_cast<const uint8_t*>(hdr), 12), cg);
            cg = crc32(raw(g.tuples), cg);
        }
        Bytes mats;
        for (const auto& mt : me.materials) mats.insert(mats.end(), mt.begin(), mt.end());
        out.push_back("m " + label + "|" + std::to_string(i) + " " + std::to_string(me.positions.size()) + " " + std::to_string(me.normals.size()) + " " +
                      std::to_string(me.uvs.size()) + " " + std::to_string(me.colors.size()) + " " + std::to_string(me.groups.size()) + " " +
                      std::to_string(me.stride) + " " + hex8(crc32(raw(me.positions))) + " " + hex8(crc32(raw(me.normals))) + " " +
                      hex8(crc32(raw(me.uvs))) + " " + hex8(crc32(raw(me.colors))) + " " + hex8(cg) + " " + hex8(crc32(mats)));
    }
}

static void img_lines(std::vector<std::string>& out, const std::string& label, View data) {
    std::vector<ImgTexture> tex;
    try { tex = img_textures(data); } catch (const std::exception&) { out.push_back("I " + label + " ERR"); return; }
    out.push_back("I " + label + " " + std::to_string(tex.size()));
    for (const auto& t : tex) {
        std::string head = "T " + label + "|" + t.name;
        Tim2 tm(data, t.offset);
        try {
            Image im = tm.decode(false, t.swizzled);
            out.push_back(head + " " + std::to_string(tm.width()) + " " + std::to_string(tm.height()) + " " + std::to_string(tm.bpp()) + " " + hex8(crc32(im.rgba)));
        } catch (const std::exception&) {
            out.push_back(head + " " + std::to_string(tm.width()) + " " + std::to_string(tm.height()) + " " + std::to_string(tm.bpp()) + " ERR");
        }
    }
}

static void wgt_lines(std::vector<std::string>& out, const std::string& label, View data) {
    std::vector<WgtRecord> recs;
    try { recs = parse_wgt(data); } catch (const std::exception&) { out.push_back("W " + label + " ERR"); return; }
    uint32_t c = 0;
    size_t n = 0;
    for (const auto& r : recs) {
        uint32_t h[5] = {r.target, r.bone, r.type, r.unk3, r.more};
        c = crc32(View(reinterpret_cast<const uint8_t*>(h), 20), c);
        for (const auto& e : r.entries) {
            uint8_t b[8];
            std::memcpy(b, &e.vertex, 4);
            std::memcpy(b + 4, &e.weight, 4);
            c = crc32(View(b, 8), c);
            ++n;
        }
    }
    out.push_back("W " + label + " " + std::to_string(recs.size()) + " " + std::to_string(n) + " " + hex8(c));
}

static void tm2_lines(std::vector<std::string>& out, const std::string& label, View data) {
    if (data.size() >= 4 && std::memcmp(data.data(), "TIM2", 4) == 0 && Tim2::probe(data, 0)) {
        Tim2 t(data, 0);
        try {
            Image im = t.decode(false);
            out.push_back("T " + label + "|0 " + std::to_string(t.width()) + " " + std::to_string(t.height()) + " " + std::to_string(t.bpp()) + " " + hex8(crc32(im.rgba)));
        } catch (const std::exception&) {
            out.push_back("T " + label + "|0 ERR");
        }
    }
}

static void classify(std::vector<std::string>& out, const std::string& label, const std::string& name, View data) {
    std::string n = lower(name);
    if (ends_with(n, ".mds")) mds_lines(out, label, data);
    else if (ends_with(n, ".img")) img_lines(out, label, data);
    else if (ends_with(n, ".wgt")) wgt_lines(out, label, data);
    else if (ends_with(n, ".tm2")) tm2_lines(out, label, data);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: dcgolden <iso> <out.txt> [--limit-files N]\n"); return 2; }
    size_t limit = 0;
    for (int i = 3; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--limit-files") == 0) limit = std::strtoull(argv[i + 1], nullptr, 10);
    try {
        Disc disc(argv[1]);
        std::vector<std::string> lines;
        const auto& es = disc.entries();
        for (size_t i = 0; i < es.size(); ++i) {
            if (limit && i >= limit) break;
            Bytes data = disc.read(es[i]);
            lines.push_back("F " + std::to_string(i) + " " + std::to_string(es[i].size) + " " + hex8(crc32(data)));
            std::string name = utf8(es[i].name);
            std::string lo = lower(es[i].name);
            if (ends_with(lo, ".chr") || ends_with(lo, ".pak") || ends_with(lo, ".pac") || ends_with(lo, ".mpd")) {
                try {
                    Pack pk(data);
                    lines.push_back("P " + name + " " + std::to_string(pk.entries().size()));
                    for (const auto& en : pk.entries()) {
                        View d = pk.data(en);
                        std::string label = name + "|" + utf8(en.name);
                        lines.push_back("E " + label + " " + std::to_string(en.size) + " " + hex8(crc32(d)));
                        classify(lines, label, en.name, d);
                    }
                } catch (const std::exception&) {
                    lines.push_back("P " + name + " ERR");
                }
            } else {
                classify(lines, name, es[i].name, data);
            }
            if (i % 500 == 0) std::fprintf(stderr, "%zu / %zu\n", i, es.size());
        }
        std::ofstream f(argv[2], std::ios::binary);
        for (const auto& l : lines) f << l << '\n';
        std::printf("wrote %s (%zu lines)\n", argv[2], lines.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
