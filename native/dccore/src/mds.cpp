#include "dccore/mds.h"

namespace dccore {

namespace {
constexpr size_t kMatSize = 0x60;

std::vector<Vec4> vec4s(View b, int64_t off, uint32_t n) {
    if (off < 0 || uint64_t(off) + uint64_t(n) * 16 > b.size()) throw Error("vertex block out of range");
    std::vector<Vec4> v(n);
    if (n) std::memcpy(v.data(), b.data() + off, size_t(n) * 16);
    return v;
}

Mesh parse_mesh(View m, size_t off, bool strict) {
    uint32_t h[16];
    if (off > m.size() || m.size() - off < 64) throw Error("mesh header out of range");
    std::memcpy(h, m.data() + off, 64);
    if (h[0] != 0x54444D) throw Error("bad MDT magic");
    // python: mt = m[off:off+h[2]]  (silently truncated)
    size_t mt_len = std::min<uint64_t>(h[2], m.size() - off);
    View mt = m.subspan(off, mt_len);
    uint32_t npos = h[3], nnorm = h[5], ncol = h[7], nuv = h[11], nmat = h[13];
    // The game decides the tuple size from the colour block OFFSET being positive (CreateVUdataFromMDT), not from the count.
    bool has_col = rd<int32_t>(mt, 0x20) > 0;
    Mesh mesh;
    mesh.stride = has_col ? 4 : 3;
    mesh.positions = vec4s(mt, h[4], npos);
    mesh.normals = vec4s(mt, h[6], nnorm);
    if (has_col) mesh.colors = vec4s(mt, h[8], ncol);
    mesh.uvs = vec4s(mt, h[12], nuv);

    uint32_t g0 = h[10];
    uint32_t ngroups = rd<uint32_t>(mt, size_t(g0) + 8);
    size_t p = size_t(g0) + 0x10;
    const size_t stride = size_t(mesh.stride);
    for (uint32_t i = 0; i < ngroups; ++i) {
        Group g;
        g.type = rd<int32_t>(mt, p);
        int32_t cnt = rd<int32_t>(mt, p + 4);
        g.material = rd<int32_t>(mt, p + 8);
        p += 12;
        if (cnt < 0) throw Error("negative tuple count");
        size_t nints = size_t(cnt) * stride;
        View t = sub(mt, p, nints * 4);
        g.tuples.resize(nints);
        if (nints) std::memcpy(g.tuples.data(), t.data(), nints * 4);
        p += nints * 4;
        mesh.groups.push_back(std::move(g));
    }
    if (p - g0 != h[9]) throw Error("group list size mismatch");
    for (uint32_t i = 0; i < nmat; ++i) {
        size_t s = size_t(h[14]) + kMatSize * i;
        // python slices silently; a short slice is caught by the block-overlap check below
        if (s > mt.size()) { mesh.materials.emplace_back(); continue; }
        View v = mt.subspan(s, std::min(kMatSize, mt.size() - s));
        mesh.materials.emplace_back(v.begin(), v.end());
    }
    // Blocks must be in this order and must not overlap.
    struct Block { const char* name; uint64_t start, size; };
    std::vector<Block> layout = {{"pos", h[4], uint64_t(16) * npos}, {"groups", g0, h[9]}, {"normals", h[6], uint64_t(16) * nnorm}};
    if (ncol > 0) layout.push_back({"colors", h[8], uint64_t(16) * ncol});
    layout.push_back({"uv", h[12], uint64_t(16) * nuv});
    layout.push_back({"materials", h[14], kMatSize * uint64_t(nmat)});
    if (h[4] != 0x40) throw Error("unexpected positions offset");
    for (size_t i = 0; strict && i < layout.size(); ++i) {
        uint64_t nxt = (i + 1 < layout.size()) ? layout[i + 1].start : mt.size();
        if (layout[i].start + layout[i].size > nxt) throw Error(std::string("block ") + layout[i].name + " overlaps the next one");
    }
    return mesh;
}
}  // namespace

std::string Mesh::texture_name(size_t material) const {
    if (material >= materials.size() || materials[material].size() < 0x54) return "";
    return cstr(materials[material], 0x34, 0x20);
}

Mds parse_mds(View d, bool strict) {
    if (d.size() < 16 || (std::memcmp(d.data(), "MDS\0", 4) != 0)) throw Error("not an MDS file");
    Mds m;
    m.version = rd<uint32_t>(d, 4);
    uint32_t nobj = rd<uint32_t>(d, 8), tab = rd<uint32_t>(d, 12);
    for (uint32_t i = 0; i < nobj; ++i) {
        size_t r = size_t(tab) + size_t(i) * 0x70;
        View raw = sub(d, r, 0x70);
        Object o;
        o.name = cstr(raw, 8, 0x20);
        uint32_t moff = rd<uint32_t>(raw, 0x28);
        o.parent = rd<int32_t>(raw, 0x2C);
        for (int k = 0; k < 16; ++k) o.matrix[k] = rd<float>(raw, 0x30 + 4 * k);
        if (moff) {
            o.mesh = parse_mesh(d, moff, strict);
            o.has_mesh = true;
        }
        m.objects.push_back(std::move(o));
    }
    return m;
}

std::vector<std::array<double, 16>> world_matrices(const Mds& m) {
    std::vector<std::array<double, 16>> w(m.objects.size());
    for (size_t i = 0; i < m.objects.size(); ++i) {
        std::array<double, 16> local;
        for (int k = 0; k < 16; ++k) local[k] = m.objects[i].matrix[k];
        int p = m.objects[i].parent;
        if (p < 0 || size_t(p) >= i) { w[i] = local; continue; }  // roots (and, defensively, forward references)
        std::array<double, 16> r{};
        for (int a = 0; a < 4; ++a)
            for (int c = 0; c < 4; ++c) {
                double s = 0;
                for (int k = 0; k < 4; ++k) s += local[a * 4 + k] * w[p][k * 4 + c];
                r[a * 4 + c] = s;
            }
        w[i] = r;
    }
    return w;
}

std::vector<Triangle> triangulate(const Group& g, int stride) {
    const size_t n = g.tuples.size() / size_t(stride);
    auto tuple = [&](size_t i) {
        const int32_t* t = &g.tuples[i * stride];
        return std::array<int32_t, 4>{t[0], t[1], t[2], stride == 4 ? t[3] : -1};
    };
    std::vector<Triangle> out;
    auto add = [&](size_t a, size_t b, size_t c) {
        auto A = tuple(a), B = tuple(b), C = tuple(c);
        if (A[0] == B[0] || B[0] == C[0] || A[0] == C[0]) return;  // degenerate
        Triangle t;
        for (int k = 0; k < 3; ++k) {
            const auto& v = (k == 0) ? A : (k == 1 ? B : C);
            t.pos[k] = v[0]; t.nrm[k] = v[1]; t.uv[k] = v[2]; t.col[k] = v[3];
        }
        out.push_back(t);
    };
    if (g.type == 3) {
        for (size_t i = 0; i + 2 < n; i += 3) add(i, i + 1, i + 2);
    } else {
        for (size_t i = 0; i + 2 < n; ++i) (i % 2 == 0) ? add(i, i + 1, i + 2) : add(i + 1, i, i + 2);
    }
    return out;
}

}  // namespace dccore
