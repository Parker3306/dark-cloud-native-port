#include "dccore/motion.h"
#include <functional>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

namespace dccore {

Mat4 mat_identity() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1.0;
    return m;
}

Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    return r;
}

Mat4 mat_inverse(const Mat4& m) {
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) a[i][j] = (j < 4) ? m[i * 4 + j] : (j - 4 == i ? 1.0 : 0.0);
    for (int c = 0; c < 4; ++c) {
        int p = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        if (std::fabs(a[p][c]) < 1e-12) return mat_identity();  // singular: should not happen for bind matrices
        if (p != c) for (int j = 0; j < 8; ++j) std::swap(a[p][j], a[c][j]);
        double d = a[c][c];
        for (int j = 0; j < 8; ++j) a[c][j] /= d;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            double f = a[r][c];
            if (f != 0) for (int j = 0; j < 8; ++j) a[r][j] -= f * a[c][j];
        }
    }
    Mat4 out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) out[i * 4 + j] = a[i][4 + j];
    return out;
}

std::vector<MotTrack> parse_mot(View d) {
    std::vector<MotTrack> out;
    size_t pos = 0;
    while (pos + 0x20 <= d.size()) {
        MotTrack t;
        t.object = rd<uint32_t>(d, pos);
        t.param = rd<uint32_t>(d, pos + 4);
        t.type = rd<uint32_t>(d, pos + 8);
        uint32_t count = rd<uint32_t>(d, pos + 16);
        uint32_t more = rd<uint32_t>(d, pos + 20);
        pos += 0x20;
        t.keys.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            MotKey k;
            k.time = float(rd<uint32_t>(d, pos));  // the key time is an integer frame number stored as a u32
            for (int c = 0; c < 4; ++c) k.v[c] = rd<float>(d, pos + 0x10 + 4 * c);
            t.keys.push_back(k);
            pos += 0x20;
        }
        out.push_back(std::move(t));
        if (!more) break;
    }
    return out;
}

std::vector<Mat4> parse_bbp(View d) {
    std::vector<Mat4> out;
    for (size_t i = 0; i + 64 <= d.size(); i += 64) {
        Mat4 m;
        for (int k = 0; k < 16; ++k) m[k] = rd<float>(d, i + 4 * k);
        out.push_back(m);
    }
    return out;
}

std::vector<MotionEntry> cfg_motion_table(View text) {
    std::vector<MotionEntry> out;
    std::string s(reinterpret_cast<const char*>(text.data()), text.size());
    bool in_block = false;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        std::string line = s.substr(pos, eol - pos);
        pos = eol + 1;
        size_t c = line.find("//");
        if (c != std::string::npos) line.resize(c);
        size_t b = 0;
        while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
        auto token_is = [&](const char* kw) {
            size_t n = std::strlen(kw);
            return line.compare(b, n, kw) == 0 && (b + n >= line.size() || !(std::isalnum(static_cast<unsigned char>(line[b + n])) || line[b + n] == '_'));
        };
        if (token_is("KEY_START")) { in_block = true; continue; }
        if (token_is("MOTION_END")) { in_block = false; continue; }
        if (!in_block || !token_is("KEY")) continue;
        std::string rest = line.substr(b + 3);
        for (char& ch : rest) if (ch == ',') ch = ' ';
        float v[3] = {0, 0, 0};
        int n = 0;
        const char* p = rest.c_str();
        char* end = nullptr;
        while (n < 3) {
            float x = std::strtof(p, &end);
            if (end == p) break;
            v[n++] = x;
            p = end;
        }
        if (n == 3) out.push_back({v[0], v[1], v[2]});
    }
    return out;
}

Skeleton make_skeleton(const Mds& mds, const std::vector<Mat4>& bbp) {
    Skeleton sk;
    const size_t n = mds.objects.size();
    sk.names.resize(n);
    sk.parent.resize(n);
    sk.rest_local.resize(n);
    sk.bind_local.resize(n);
    sk.bind_world.resize(n);
    sk.bind_inverse.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Object& o = mds.objects[i];
        sk.names[i] = o.name;
        sk.parent[i] = o.parent;
        for (int k = 0; k < 16; ++k) sk.rest_local[i][k] = o.matrix[k];
        sk.bind_local[i] = i < bbp.size() ? bbp[i] : sk.rest_local[i];
        {   // some .bbp entries are all zero (Xiao's dummy root, every rigid part): treat a degenerate matrix as identity
            const Mat4& b = sk.bind_local[i];
            double det = b[0] * (b[5] * b[10] - b[6] * b[9]) - b[1] * (b[4] * b[10] - b[6] * b[8]) + b[2] * (b[4] * b[9] - b[5] * b[8]);
            if (std::fabs(det) < 1e-6) sk.bind_local[i] = mat_identity();
        }
        bool root = o.parent < 0 || size_t(o.parent) >= i;
        sk.bind_world[i] = root ? sk.bind_local[i] : mat_mul(sk.bind_local[i], sk.bind_world[o.parent]);
        sk.bind_inverse[i] = mat_inverse(sk.bind_world[i]);
    }
    return sk;
}

namespace {
constexpr double kEpsLow = 1e-4, kEpsHigh = 0.9999, kSlerpEps = 0.001;

// Interval of a track at time t: keys k0 (at or before) and k1 (after), and the blend factor. Clamps outside the key range.
struct Interval { size_t k0, k1; double f; };
Interval find_interval(const std::vector<MotKey>& keys, double t) {
    const double ft = std::floor(t);
    size_t lo = 0, hi = keys.size();
    while (lo < hi) {  // first key whose integer time is greater than floor(t)
        size_t mid = (lo + hi) / 2;
        if (std::floor(double(keys[mid].time)) <= ft) lo = mid + 1; else hi = mid;
    }
    if (lo == 0) return {0, 0, 0.0};
    if (lo >= keys.size()) return {keys.size() - 1, keys.size() - 1, 0.0};
    size_t k0 = lo - 1, k1 = lo;
    double t0 = keys[k0].time, t1 = keys[k1].time;
    double f = (t1 == t0) ? 0.0 : (t - t0) / (t1 - t0);
    return {k0, k1, std::clamp(f, 0.0, 1.0)};
}

void lerp3(const MotKey& a, const MotKey& b, double f, double out[3]) {
    const MotKey* use = nullptr;
    if (f <= kEpsLow) use = &a; else if (f >= kEpsHigh) use = &b;
    for (int c = 0; c < 3; ++c) out[c] = use ? use->v[c + 0] : (1.0 - f) * a.v[c] + f * b.v[c];
}

// MotionProc type 0: both quaternions get their first component negated, then slerp; the result is turned into a rotation matrix by QuatToMat.
std::array<double, 4> quat_at(const MotKey& a, const MotKey& b, double f) {
    double qa[4] = {-double(a.v[0]), a.v[1], a.v[2], a.v[3]};
    double qb[4] = {-double(b.v[0]), b.v[1], b.v[2], b.v[3]};
    if (f <= kEpsLow) return {qa[0], qa[1], qa[2], qa[3]};
    if (f >= kEpsHigh) return {qb[0], qb[1], qb[2], qb[3]};
    double dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (dot < 0) { dot = -dot; for (double& x : qb) x = -x; }
    double c0, c1;
    if (1.0 - dot > kSlerpEps) {
        double th = std::acos(dot), s = 1.0 / std::sin(th);
        c0 = s * std::sin((1.0 - f) * th);
        c1 = s * std::sin(f * th);
    } else {
        c0 = 1.0 - f;
        c1 = f;
    }
    return {c0 * qa[0] + c1 * qb[0], c0 * qa[1] + c1 * qb[1], c0 * qa[2] + c1 * qb[2], c0 * qa[3] + c1 * qb[3]};
}

// QuatToMat: q = (w, x, y, z) -> rotation rows 0..2 of a row-vector matrix (translation row untouched).
void quat_to_rows(const std::array<double, 4>& q, Mat4& m) {
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    const double yy2 = y * (y + y), zz2 = z * (z + z), xx2 = x * (x + x);
    m[0] = 1.0 - (yy2 + zz2);
    m[4] = x * (y + y) - w * (z + z);
    m[8] = x * (z + z) + w * (y + y);
    m[1] = x * (y + y) + w * (z + z);
    m[5] = 1.0 - (xx2 + zz2);
    m[9] = y * (z + z) - w * (x + x);
    m[2] = x * (z + z) - w * (y + y);
    m[6] = y * (z + z) + w * (x + x);
    m[10] = 1.0 - (xx2 + yy2);
    m[3] = m[7] = m[11] = 0.0;
}
}  // namespace

Pose rest_pose(const Skeleton& sk) {
    Pose p;
    const size_t n = sk.size();
    p.local = sk.rest_local;
    p.world.resize(n);
    for (size_t i = 0; i < n; ++i) {
        bool root = sk.parent[i] < 0 || size_t(sk.parent[i]) >= i;
        p.world[i] = root ? p.local[i] : mat_mul(p.local[i], p.world[sk.parent[i]]);
    }
    return p;
}

namespace {
Pose pose_from_tracks(const Skeleton& sk, const std::vector<MotTrack>& tracks, const std::function<Interval(const std::vector<MotKey>&)>& pick) {
    Pose p;
    const size_t n = sk.size();
    p.local = sk.rest_local;
    std::vector<std::array<double, 3>> scale(n, {1.0, 1.0, 1.0});
    std::vector<char> scaled(n, 0);
    for (const MotTrack& t : tracks) {
        if (t.object >= n || t.keys.empty()) continue;
        Mat4& m = p.local[t.object];
        const Interval iv = pick(t.keys);
        const MotKey& a = t.keys[iv.k0];
        const MotKey& b = t.keys[iv.k1];
        if (t.type == 0) {
            quat_to_rows(quat_at(a, b, iv.f), m);
        } else if (t.type == 2) {
            double v[3];
            lerp3(a, b, iv.f, v);
            m[12] = v[0]; m[13] = v[1]; m[14] = v[2];
        } else if (t.type == 1) {
            double v[3];
            lerp3(a, b, iv.f, v);
            scale[t.object] = {v[0], v[1], v[2]};
            scaled[t.object] = 1;
        }
    }
    p.world.resize(n);
    for (size_t i = 0; i < n; ++i) {
        Mat4 m = p.local[i];
        if (scaled[i])
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) m[r * 4 + c] *= scale[i][r];
        p.local[i] = m;
        const bool root = sk.parent[i] < 0 || size_t(sk.parent[i]) >= i;
        p.world[i] = root ? m : mat_mul(m, p.world[sk.parent[i]]);
    }
    return p;
}
}  // namespace

Pose evaluate_pose_blend(const Skeleton& sk, const std::vector<MotTrack>& tracks, double frame, double next, double frac) {
    return pose_from_tracks(sk, tracks, [&](const std::vector<MotKey>& keys) {
        const Interval a = find_interval(keys, frame), b = find_interval(keys, next);
        return Interval{a.k0, b.k0, std::clamp(frac, 0.0, 1.0)};
    });
}

Pose evaluate_pose(const Skeleton& sk, const std::vector<MotTrack>& tracks, double time) {
    Pose p;
    const size_t n = sk.size();
    p.local = sk.rest_local;
    std::vector<std::array<double, 3>> scale(n, {1.0, 1.0, 1.0});
    std::vector<char> scaled(n, 0);
    for (const MotTrack& t : tracks) {
        if (t.object >= n || t.keys.empty()) continue;
        Mat4& m = p.local[t.object];
        Interval iv = find_interval(t.keys, time);
        const MotKey& a = t.keys[iv.k0];
        const MotKey& b = t.keys[iv.k1];
        if (t.type == 0) {
            quat_to_rows(quat_at(a, b, iv.f), m);
        } else if (t.type == 2) {
            double v[3];
            lerp3(a, b, iv.f, v);
            m[12] = v[0]; m[13] = v[1]; m[14] = v[2];
        } else if (t.type == 1) {
            double v[3];
            lerp3(a, b, iv.f, v);
            scale[t.object] = {v[0], v[1], v[2]};
            scaled[t.object] = 1;
        }
    }
    p.world.resize(n);
    for (size_t i = 0; i < n; ++i) {
        Mat4 m = p.local[i];
        if (scaled[i])  // CFrame::GetLWMatrix multiplies the rotation rows by the scale vector
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) m[r * 4 + c] *= scale[i][r];
        p.local[i] = m;
        bool root = sk.parent[i] < 0 || size_t(sk.parent[i]) >= i;
        p.world[i] = root ? m : mat_mul(m, p.world[sk.parent[i]]);
    }
    return p;
}

std::vector<Mat4> skin_matrices(const Skeleton& sk, const Pose& pose, int root) {
    const size_t n = sk.size();
    std::vector<Mat4> out(n, mat_identity());
    if (root < 0 || size_t(root) >= n) return out;
    const Mat4 inv_a_root = mat_inverse(pose.world[root]);
    const Mat4 inv_b_root = mat_inverse(sk.bind_world[root]);
    for (size_t i = 0; i < n; ++i) {
        Mat4 a = mat_mul(pose.world[i], inv_a_root);        // animated world relative to the root
        Mat4 b = mat_mul(sk.bind_world[i], inv_b_root);     // bind world relative to the root
        out[i] = mat_mul(mat_inverse(b), a);
    }
    return out;
}

Vec3d skin_point(const Vec3d& rest, const std::map<uint32_t, float>& bones, const std::vector<Mat4>& mats) {
    Vec3d out = rest;
    for (const auto& [bone, pct] : bones) {
        if (bone >= mats.size()) continue;
        const Mat4& m = mats[bone];
        double x = rest.x * m[0] + rest.y * m[4] + rest.z * m[8] + m[12];
        double y = rest.x * m[1] + rest.y * m[5] + rest.z * m[9] + m[13];
        double z = rest.x * m[2] + rest.y * m[6] + rest.z * m[10] + m[14];
        double w = double(pct) * 0.01;
        out.x += (x - rest.x) * w;
        out.y += (y - rest.y) * w;
        out.z += (z - rest.z) * w;
    }
    return out;
}


bool sample_camera_tracks(const std::vector<MotTrack>& tracks, double time, float pos[3], float ref[3], float* fov) {
    bool any = false;
    auto sample = [&](uint32_t type, float out[4]) {
        for (const MotTrack& t : tracks) {
            if (t.type != type || t.keys.empty()) continue;
            const auto& k = t.keys;
            if (time <= double(k.front().time)) { for (int i = 0; i < 4; ++i) out[i] = k.front().v[i]; return true; }
            if (time >= double(k.back().time)) { for (int i = 0; i < 4; ++i) out[i] = k.back().v[i]; return true; }
            size_t hi = 1;
            while (hi < k.size() && double(k[hi].time) < time) ++hi;
            const auto& a = k[hi - 1];
            const auto& b = k[hi];
            const double span = double(b.time) - double(a.time);
            const float f = span > 0 ? float((time - double(a.time)) / span) : 0.0f;
            for (int i = 0; i < 4; ++i) out[i] = a.v[i] + (b.v[i] - a.v[i]) * f;
            return true;
        }
        return false;
    };
    float v[4];
    if (sample(0x1e, v)) { pos[0] = v[0]; pos[1] = v[1]; pos[2] = v[2]; any = true; }
    if (sample(0x1f, v)) { ref[0] = v[0]; ref[1] = v[1]; ref[2] = v[2]; any = true; }
    if (fov && sample(0x21, v)) *fov = v[0];
    return any;
}

}  // namespace dccore
