#include "dccore/collision.h"
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "dccore/gamemath.h"
#include "dccore/vu0.h"

namespace dccore {

// All arithmetic follows the game: vector helpers are the VU0 library (truncating), scalar code runs on the EE FPU (see gamemath.h).
namespace {

using V3 = vu0::V3;

constexpr float kEps01 = 0.1f;        // DAT_002a1870
constexpr float kStepX = 0.01f;       // DAT_002a1a08
constexpr float kStepZ = 0.001f;      // DAT_002a1894
constexpr float kSqrt2 = 1.4142135f;  // DAT_002a1a0c

using gm::fadd;
using gm::fsub;
using gm::fmul;
using gm::fdiv;

void normalize3(float* v) {   // sceVu0Normalize in place (xyz)
    const V3 r = vu0::normalize(v);
    v[0] = r[0]; v[1] = r[1]; v[2] = r[2];
}

// Check_Point_Poly3_XYZ: is the point (on the polygon's plane) inside the triangle? Border counts as inside.
bool point_in_poly(const float* p, const float* a, const float* b, const float* c, const float* n) {
    const V3 pa = vu0::sub(p, a), pb = vu0::sub(p, b), pc = vu0::sub(p, c);
    const V3 ab = vu0::sub(b, a), bc = vu0::sub(c, b), ca = vu0::sub(a, c);
    const V3 o0 = vu0::outer(ab.data(), pa.data()), o1 = vu0::outer(bc.data(), pb.data()), o2 = vu0::outer(ca.data(), pc.data());
    const float f0 = vu0::inner(o0.data(), n), f1 = vu0::inner(o1.data(), n), f2 = vu0::inner(o2.data(), n);
    if (f0 < 0 || f1 < 0 || f2 < 0) return !(f0 > 0 || f1 > 0 || f2 > 0);
    return true;
}

// IntersectionPoint_line_poly3: the infinite line p1-p2 against the polygon's plane, then the inside test.
bool line_poly(const float* p1, const float* p2, const CCPoly& q, float* out) {
    const V3 dir = vu0::sub(p2, p1), to_v0 = vu0::sub(q.v[0], p1);
    const float num = vu0::inner(q.n, to_v0.data()), den = vu0::inner(q.n, dir.data());
    if (den == 0.0f) return false;
    const float t = fdiv(num, den);
    const V3 sc = vu0::scale(dir.data(), t);
    const V3 r = vu0::add(sc.data(), p1);
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2];
    return point_in_poly(out, q.v[0], q.v[1], q.v[2], q.n);
}

void tri_bounds(const CCPoly& q, float* mn, float* mx) {
    for (int k = 0; k < 3; ++k) {
        mn[k] = std::min({q.v[0][k], q.v[1][k], q.v[2][k]});
        mx[k] = std::max({q.v[0][k], q.v[1][k], q.v[2][k]});
    }
}

bool boxes_overlap(const float* amn, const float* amx, const float* bmn, const float* bmx) {
    for (int k = 0; k < 3; ++k)
        if (amn[k] > bmx[k] || bmn[k] > amx[k]) return false;
    return true;
}

}  // namespace

int check_hit(const CCPoly* polys, int n, const float p1[3], const float p2[3], float hit[3], bool nearest, int mask) {
    float smn[3], smx[3];
    for (int k = 0; k < 3; ++k) { smn[k] = std::min(p1[k], p2[k]); smx[k] = std::max(p1[k], p2[k]); }
    int best = -1;
    float best_d = 0;
    bool have = false;
    for (int i = 0; i < n; ++i) {
        const CCPoly& q = polys[i];
        if ((q.type() & mask) != 0) continue;
        float tmn[3], tmx[3];
        tri_bounds(q, tmn, tmx);
        if (!boxes_overlap(smn, smx, tmn, tmx)) continue;
        const V3 s1 = vu0::sub(p1, q.v[0]), s2 = vu0::sub(p2, q.v[0]);
        const float d1 = vu0::inner(q.n, s1.data()), d2 = vu0::inner(q.n, s2.data());
        if (!((d1 <= 0.0f || d2 <= 0.0f) && (0.0f <= d1 || 0.0f <= d2))) continue;
        float h[3];
        if (!line_poly(p1, p2, q, h)) continue;
        if (!nearest) {
            std::memcpy(hit, h, sizeof h);
            return i;
        }
        const float dx = fsub(p1[0], h[0]), dy = fsub(p1[1], h[1]), dz = fsub(p1[2], h[2]);
        const float dd = fadd(fadd(fmul(dz, dz), fmul(dx, dx)), fmul(dy, dy));
        if (!have || dd < best_d) {
            std::memcpy(hit, h, sizeof h);
            best_d = dd;
            best = i;
        }
        have = true;
    }
    return best;
}

int check_hits(const CCPoly* polys, int n, const float p1[3], const float p2[3], int max_hits, int* index_out, float (*hit_out)[4], int sort, int mask) {
    float smn[3], smx[3];
    for (int k = 0; k < 3; ++k) { smn[k] = std::min(p1[k], p2[k]); smx[k] = std::max(p1[k], p2[k]); }
    int count = 0;
    for (int i = 0; i < n; ++i) {
        const CCPoly& q = polys[i];
        if ((q.type() & mask) != 0) continue;
        float tmn[3], tmx[3];
        tri_bounds(q, tmn, tmx);
        if (!boxes_overlap(smn, smx, tmn, tmx)) continue;
        const V3 s1 = vu0::sub(p1, q.v[0]), s2 = vu0::sub(p2, q.v[0]);
        const float d1 = vu0::inner(q.n, s1.data()), d2 = vu0::inner(q.n, s2.data());
        if (!((d1 <= 0.0f || d2 <= 0.0f) && (0.0f <= d1 || 0.0f <= d2))) continue;
        float h[3];
        if (!line_poly(p1, p2, q, h)) continue;
        if (max_hits <= count) break;
        index_out[count] = i;
        hit_out[count][0] = h[0];
        hit_out[count][1] = h[1];
        hit_out[count][2] = h[2];
        hit_out[count][3] = vu0::dist(p1, h);
        ++count;
    }
    if (sort != 0) {  // the game's exchange sort on the distance stored in w
        for (int i = 0; i < count - 1; ++i)
            for (int j = i + 1; j < count; ++j)
                if (hit_out[j][3] < hit_out[i][3]) {
                    std::swap(index_out[i], index_out[j]);
                    for (int k = 0; k < 4; ++k) std::swap(hit_out[i][k], hit_out[j][k]);
                }
    }
    return count;
}

int check_hit_vertical(float up, const CCPoly* polys, int n, const float p[3], float hit[3], int mask) {
    const float q[3] = {p[0], fadd(p[1], up), p[2]};
    int best = -1;
    float best_y = 0;
    for (int i = 0; i < n; ++i) {
        const CCPoly& poly = polys[i];
        if ((poly.type() & mask) != 0) continue;
        if (!line_poly(p, q, poly, hit)) continue;
        const float y = hit[1];
        if (up <= 0.0f) {
            if (y < p[1] && (best < 0 || !(y < best_y))) { best = i; best_y = y; }
        } else {
            if (p[1] < y && (best < 0 || !(best_y < y))) { best = i; best_y = y; }
        }
    }
    if (best >= 0) hit[1] = best_y;
    return best;
}

namespace {

// One probe of CheckWidth: segment from c to c+d; true (and the hit point) if it meets a polygon that is steep enough to be a wall (|ny| < 0.5).
bool wall_probe(const CCPoly* polys, int n, const float c[3], const float d[3], float hit[3], int mask) {
    const float e[3] = {fadd(c[0], d[0]), fadd(c[1], d[1]), fadd(c[2], d[2])};
    const int i = check_hit(polys, n, c, e, hit, false, mask);
    if (i < 0) return false;
    const V3 nn = vu0::normalize(polys[i].n);   // (into a temporary: CheckWidth leaves the polygon alone)
    const float ny = nn[1];
    return ny < 0.5f && -0.5f < ny;
}

}  // namespace

int check_width(float r, const CCPoly* polys, int n, const float pos[3], float out[3], int mask) {
    const float rd = fdiv(r, kSqrt2);
    float c[3] = {pos[0], pos[1], pos[2]};
    out[0] = pos[0];
    out[1] = pos[1];
    out[2] = pos[2];
    int touched = 0;
    float ha[3], hb[3];
    auto mid = [](float a, float b) { return fmul(fadd(a, b), 0.5f); };

    // diagonal (+x+z) / (-x-z)
    {
        const float da[3] = {rd, 0, rd}, db[3] = {-rd, 0, -rd};
        const bool a = wall_probe(polys, n, c, da, ha, mask), b = wall_probe(polys, n, c, db, hb, mask);
        if (a) touched = 1;
        if (b) touched = 1;
        if (a && b) {
            out[0] = mid(ha[0], hb[0]);
            out[2] = mid(ha[2], hb[2]);
        } else {
            if (a) { out[0] = fsub(ha[0], rd); out[2] = fsub(ha[2], rd); }
            if (b) { out[0] = fadd(hb[0], rd); out[2] = fadd(hb[2], rd); }
        }
    }
    // diagonal (+x-z) / (-x+z) from the corrected position
    c[0] = out[0]; c[1] = out[1]; c[2] = out[2];
    {
        const float da[3] = {rd, 0, -rd}, db[3] = {-rd, 0, rd};
        const bool a = wall_probe(polys, n, c, da, ha, mask), b = wall_probe(polys, n, c, db, hb, mask);
        if (a) touched = 1;
        if (b) touched = 1;
        if (a && b) {
            out[0] = mid(ha[0], hb[0]);
            out[2] = mid(ha[2], hb[2]);
        } else {
            if (a) { out[0] = fsub(ha[0], rd); out[2] = fadd(ha[2], rd); }
            if (b) { out[0] = fadd(hb[0], rd); out[2] = fsub(hb[2], rd); }
        }
    }
    // x axis
    c[0] = out[0]; c[1] = out[1]; c[2] = out[2];
    {
        const float da[3] = {r, 0, 0}, db[3] = {-r, 0, 0};
        const bool a = wall_probe(polys, n, c, da, ha, mask), b = wall_probe(polys, n, c, db, hb, mask);
        if (a) touched = 1;
        if (b) touched = 1;
        if (a && b) out[0] = mid(ha[0], hb[0]);
        else {
            if (a) out[0] = fsub(ha[0], r);
            if (b) out[0] = fadd(hb[0], r);
        }
    }
    // z axis
    c[0] = out[0]; c[1] = out[1]; c[2] = out[2];
    {
        const float da[3] = {0, 0, r}, db[3] = {0, 0, -r};
        const bool a = wall_probe(polys, n, c, da, ha, mask), b = wall_probe(polys, n, c, db, hb, mask);
        if (a) touched = 1;
        if (b) touched = 1;
        if (a && b) out[2] = mid(ha[2], hb[2]);
        else {
            if (a) out[2] = fsub(ha[2], r);
            if (b) out[2] = fadd(hb[2], r);
        }
    }
    return touched;
}

bool get_foot_poly(float range, const float p[3], CCPoly& poly, float hit[3], const CCPoly* polys, int n, int mask) {
    const float a[3] = {p[0], p[1], p[2]};
    const float b[3] = {p[0], fsub(p[1], range), p[2]};
    int idx[32];
    float hits[32][4];
    const int count = check_hits(polys, n, a, b, 32, idx, hits, 1, mask);
    uint16_t t[3] = {0, 0, 0};
    if (count > 0) {
        poly = polys[idx[0]];
        hit[0] = hits[0][0];
        hit[1] = hits[0][1];
        hit[2] = hits[0][2];
    }
    // the attribute of the polygon is rebuilt from the first non-zero value of each of its three halfwords over all hit polygons
    for (int i = 0; i < count; ++i) {
        const CCPoly& q = polys[idx[i]];
        if (t[0] == 0) t[0] = uint16_t(q.attr[0] & 0xFFFF);
        if (t[1] == 0) t[1] = uint16_t(q.attr[0] >> 16);
        if (t[2] == 0) t[2] = uint16_t(q.attr[1] & 0xFFFF);
    }
    if (count > 0) {
        poly.attr[0] = uint32_t(t[0]) | (uint32_t(t[1]) << 16);
        poly.attr[1] = uint32_t(t[2]) | (poly.attr[1] & 0xFFFF0000u);
    }
    return count > 0;
}

bool move_check(const float pos[3], float vel[3], float out[3], MoveInfo& info, CCPoly* polys, int n, int mask) {
    out[0] = pos[0]; out[1] = pos[1]; out[2] = pos[2];
    float hit[3] = {0, 0, 0};
    // 1. headroom: the column at the destination between 17 and 4+vy above the feet must be free
    // (the game really adds vel[0] to z here, not vel[2]: both x and z of the probe use the x velocity)
    float a[3] = {fadd(pos[0], vel[0]), fadd(pos[1], 17.0f), fadd(pos[2], vel[0])};
    float b[3] = {a[0], fadd(fadd(pos[1], 4.0f), vel[1]), a[2]};
    if (check_hit(polys, n, a, b, hit, false, mask) > -1) { vel[0] = 0; vel[2] = 0; }

    // 2. the path of the feet (4 above the ground)
    float p0[3] = {pos[0], fadd(pos[1], 4.0f), pos[2]};
    float p1[3] = {fadd(p0[0], vel[0]), fadd(p0[1], vel[1]), fadd(p0[2], vel[2])};
    float start[3], dest[3];
    const int wall = check_hit(polys, n, p0, p1, hit, false, mask);
    if (wall > -1) {
        CCPoly& q = polys[wall];
        normalize3(q.n);
        if (q.n[1] >= 0.5f || q.n[1] <= -0.5f) {
            start[0] = hit[0]; start[1] = fadd(hit[1], 4.0f); start[2] = hit[2];
            dest[0] = hit[0]; dest[1] = fsub(start[1], 4.0f); dest[2] = hit[2];
        } else {
            start[0] = p0[0]; start[1] = p0[1]; start[2] = p0[2];
            dest[0] = pos[0]; dest[1] = pos[1]; dest[2] = pos[2];
        }
    } else {
        start[0] = p1[0]; start[1] = p1[1]; start[2] = p1[2];
        dest[0] = p1[0]; dest[1] = fsub(p1[1], 4.0f); dest[2] = p1[2];
    }

    // 3. the floor under the destination
    info.foot_found = 0;
    info.grounded = 0;
    const float tolerance = (kEps01 < vel[1]) ? 0.0f : 2.0f;
    float probe[3] = {start[0], start[1], start[2]};
    float floor_hit[3] = {0, 0, 0};
    CCPoly foot;
    bool found = false;
    for (int i = 0; i < 2; ++i) {
        if (get_foot_poly(15.0f, probe, foot, floor_hit, polys, n, mask)) { found = true; break; }
        probe[0] = fadd(probe[0], kStepX);
        probe[2] = fadd(probe[2], kStepZ);
    }
    if (found) {
        normalize3(foot.n);
        info.foot = foot;
        info.foot_found = 1;
        info.foot_hit[0] = floor_hit[0]; info.foot_hit[1] = floor_hit[1]; info.foot_hit[2] = floor_hit[2];
        info.grounded = (fsub(fsub(fadd(start[1], vel[1]), 4.0f), tolerance) < floor_hit[1]) ? 1 : 0;
    }
    if (info.grounded == 0) {
        out[0] = dest[0]; out[1] = dest[1]; out[2] = dest[2];
    } else {
        out[0] = floor_hit[0]; out[1] = floor_hit[1]; out[2] = floor_hit[2];
    }

    // 4. keep the body (radius 5) out of walls
    const float centre[3] = {out[0], fadd(out[1], 4.0f), out[2]};
    float pushed[3];
    if (check_width(5.0f, polys, n, centre, pushed, mask) != 0) {
        out[0] = pushed[0];
        out[2] = pushed[2];
    }
    return wall > -1;
}

}  // namespace dccore

namespace dccore {

CollisionPiece parse_collision_mds(View d) {
    CollisionPiece piece;
    if (d.size() < 16 || std::memcmp(d.data(), "MDS", 4) != 0) throw Error("not an MDS file");
    const uint32_t nobj = rd<uint32_t>(d, 8), tab = rd<uint32_t>(d, 12);
    std::vector<std::array<double, 16>> world(nobj);
    for (uint32_t i = 0; i < nobj; ++i) {
        const size_t r = size_t(tab) + size_t(i) * 0x70;
        std::array<double, 16> local;
        for (int k = 0; k < 16; ++k) local[size_t(k)] = rd<float>(d, r + 0x30 + 4 * size_t(k));
        const int32_t parent = rd<int32_t>(d, r + 0x2C);
        if (parent < 0 || uint32_t(parent) >= i) {
            world[i] = local;
        } else {
            std::array<double, 16> m{};
            for (int a = 0; a < 4; ++a)
                for (int c = 0; c < 4; ++c) {
                    double s = 0;
                    for (int k = 0; k < 4; ++k) s += local[size_t(a * 4 + k)] * world[size_t(parent)][size_t(k * 4 + c)];
                    m[size_t(a * 4 + c)] = s;
                }
            world[i] = m;
        }
    }
    std::vector<std::string> names(nobj);
    std::vector<uint8_t> is_door(nobj, 0);
    for (uint32_t i = 0; i < nobj; ++i) {
        const size_t r = size_t(tab) + size_t(i) * 0x70;
        char nm[0x21] = {};
        for (int k = 0; k < 0x20; ++k) nm[k] = char(d[r + 8 + size_t(k)]);
        names[i] = nm;
        const int32_t parent = rd<int32_t>(d, r + 0x2C);
        is_door[i] = uint8_t(door_kind_of(names[i]));
        if (!is_door[i] && parent >= 0 && uint32_t(parent) < i) is_door[i] = is_door[size_t(parent)];
        piece.frames.push_back({names[i], {float(world[i][12]), float(world[i][13]), float(world[i][14])}});
        piece.frame_yaw.push_back(float(std::atan2(world[i][8], world[i][10])));
        piece.obj_names.push_back(names[i]);
        piece.obj_parent.push_back(parent >= 0 && uint32_t(parent) < i ? parent : -1);
    }
    for (uint32_t i = 0; i < nobj; ++i) {
        const size_t r = size_t(tab) + size_t(i) * 0x70;
        const uint32_t moff = rd<uint32_t>(d, r + 0x28);
        if (!moff) continue;
        uint32_t h[16];
        for (int k = 0; k < 16; ++k) h[k] = rd<uint32_t>(d, size_t(moff) + 4 * size_t(k));
        if (h[0] != 0x54444D) throw Error("bad MDT magic");
        const size_t vtx = size_t(moff) + h[4], grp = size_t(moff) + h[10];
        const uint32_t npoly = rd<uint32_t>(d, grp + 0x14);
        const auto& m = world[i];
        auto vertex = [&](int32_t idx, float* out) {
            const size_t o = vtx + size_t(idx) * 16;
            const double x = rd<float>(d, o), y = rd<float>(d, o + 4), z = rd<float>(d, o + 8);
            out[0] = float(x * m[0] + y * m[4] + z * m[8] + m[12]);
            out[1] = float(x * m[1] + y * m[5] + z * m[9] + m[13]);
            out[2] = float(x * m[2] + y * m[6] + z * m[10] + m[14]);
            out[3] = 1.0f;
        };
        for (uint32_t k = 0; k < npoly; ++k) {
            const size_t o = grp + 0x18 + size_t(k) * 20;
            CCPoly q{};
            for (int v = 0; v < 3; ++v) vertex(rd<int32_t>(d, o + 4 * size_t(v)), q.v[v]);
            const int32_t attr = rd<int32_t>(d, o + 12);
            if (h[14] != 0 && attr >= 0)
                for (int w = 0; w < 4; ++w) q.attr[w] = rd<uint32_t>(d, size_t(moff) + h[14] + size_t(attr) * 16 + 4 * size_t(w));
            const vu0::V3 e1 = vu0::sub(q.v[1], q.v[0]), e2 = vu0::sub(q.v[2], q.v[0]);
            const vu0::V3 nn = vu0::outer(e1.data(), e2.data());
            q.n[0] = nn[0]; q.n[1] = nn[1]; q.n[2] = nn[2]; q.n[3] = 0.0f;
            piece.polys.push_back(q);
            piece.door.push_back(is_door[i]);
            piece.obj.push_back(uint16_t(i));
        }
    }
    return piece;
}

int door_kind_of(const std::string& n) {
    std::string l;
    for (char c : n) l += char(std::tolower(static_cast<unsigned char>(c)));
    if (l.rfind("ndoor", 0) == 0) return 1;
    if (l.rfind("out_door", 0) == 0 || l.rfind("outdoor", 0) == 0) return 2;
    if (l.rfind("cdoor", 0) == 0 || l == "u05_a") return 4;   // (u05_a: the black wind of Ungaga's room, lifted away by his blast)
    return 0;
}

void place_polys(const CollisionPiece& piece, int turn, const float pos[3], std::vector<CCPoly>& out, int skip_kinds, const FrameMoves* moves) {
    // rotation about +Y by -90 degrees * turn (right-handed): x' = x*cos + z*sin, z' = -x*sin + z*cos with sin = -turn, cos = 1 for turn 0
    float c = 1, s = 0;
    switch (((turn % 4) + 4) % 4) {
        case 0: c = 1; s = 0; break;
        case 1: c = 0; s = -1; break;   // -90 degrees
        case 2: c = -1; s = 0; break;
        case 3: c = 0; s = 1; break;    // +90 degrees
    }
    auto rot = [&](const float* in, float* o, bool translate) {
        const float x = in[0], z = in[2];
        o[0] = x * c + z * s + (translate ? pos[0] : 0.0f);
        o[1] = in[1] + (translate ? pos[1] : 0.0f);
        o[2] = -x * s + z * c + (translate ? pos[2] : 0.0f);
        o[3] = in[3];
    };
    std::vector<std::array<float, 3>> offs;   // per object: where a script put its frame (or the frame of a parent)
    if (moves && !moves->empty()) {
        offs.assign(piece.obj_names.size(), {0.0f, 0.0f, 0.0f});
        for (size_t i = 0; i < piece.obj_names.size(); ++i)
            for (int k = int(i); k >= 0; k = piece.obj_parent[size_t(k)]) {
                auto it = moves->find(piece.obj_names[size_t(k)]);
                if (it != moves->end()) { offs[i] = it->second; break; }
            }
    }
    for (size_t qi = 0; qi < piece.polys.size(); ++qi) {
        if (skip_kinds && qi < piece.door.size() && (piece.door[qi] & skip_kinds)) continue;
        const CCPoly& q = piece.polys[qi];
        CCPoly r = q;
        for (int v = 0; v < 3; ++v) rot(q.v[v], r.v[v], true);
        rot(q.n, r.n, false);
        if (!offs.empty() && qi < piece.obj.size()) {
            const std::array<float, 3>& o = offs[piece.obj[qi]];   // (the offset is in the part's own axes: turned like the polygon)
            float t[3];
            rot(o.data(), t, false);
            for (int v = 0; v < 3; ++v) { r.v[v][0] += t[0]; r.v[v][1] += t[1]; r.v[v][2] += t[2]; }
        }
        out.push_back(r);
    }
}

}  // namespace dccore
