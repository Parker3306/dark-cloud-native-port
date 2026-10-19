#include "dccore/villager_walk.h"
#include <algorithm>
#include <cmath>
#include "dccore/edit.h"
#include "dccore/walk.h"

namespace dccore {

namespace {

constexpr float kDirs[4][3] = {{0, 0, 100}, {100, 0, 0}, {0, 0, -100}, {-100, 0, 0}};   // +z, +x, -z, -x (ELF 0x2699f0)

bool in_area(const Town::Area& a, int cx, int cz) { return cx >= 0 && cz >= 0 && cx < a.def.width && cz < a.def.height; }
size_t cell_index(const Town::Area& a, int cx, int cz) { return size_t(cz) * size_t(a.def.width) + size_t(cx); }

float dist3(const float a[3], const float b[3]) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
}

// What stands on the cell under a point: -1 nothing (and no area), 0 open ground, else the part kind + 1 (1 building, 2 road, 3 river, 4 bridge ...).
struct CellInfo {
    bool area = false;
    bool part = false;
    int kind = 0;
};
CellInfo cell_at(const Town& t, const float pos[3]) {
    CellInfo c;
    const int ai = edit_find_area(t, pos);
    if (ai < 0) return c;
    const Town::Area& a = t.areas[size_t(ai)];
    int cx, cz;
    edit_cell(a, pos, cx, cz);
    c.area = true;
    if (!in_area(a, cx, cz)) { c.area = false; return c; }
    const size_t i = cell_index(a, cx, cz);
    c.part = a.code[i] != 0 || a.part[i] >= 0;
    c.kind = i < a.kind.size() ? int(a.kind[i]) : 0;
    return c;
}

}  // namespace

bool roam_random_plane_pos(const Town& t, const float center[3], float radius, const std::vector<std::array<float, 3>>& avoid, float avoid_r, GameRand& rng, float out[3]) {
    std::vector<std::array<float, 3>> found;
    for (const Town::Area& a : t.areas) {
        for (int cz = 2; cz < a.def.height - 2; ++cz)
            for (int cx = 2; cx < a.def.width - 2; ++cx) {
                const size_t i = cell_index(a, cx, cz);
                const bool free_cell = a.code[i] == 0 && a.part[i] < 0;
                const bool road = i < a.kind.size() && a.kind[i] == 1;
                if (!free_cell && !road) continue;
                if (a.alt[i] != 0) continue;   // flat ground only
                const std::array<float, 3> p = edit_cell_center(a, cx, cz);
                if (radius > 0.0f && dist3(p.data(), center) > radius) continue;
                bool near_avoid = false;
                for (const auto& q : avoid)
                    if (dist3(p.data(), q.data()) <= avoid_r) { near_avoid = true; break; }
                if (near_avoid) continue;
                if (found.size() < 2048) found.push_back(p);
            }
    }
    if (found.empty()) return false;
    const auto& pick = found[size_t(uint32_t(rng.next()) % uint32_t(found.size()))];
    out[0] = pick[0]; out[1] = pick[1]; out[2] = pick[2];
    return true;
}

void roam_decide(const Town& t, std::vector<Roamer>& all, size_t self, GameRand& rng) {
    Roamer& me = all[self];
    // the heading bucket of the yaw (0 = +z, 1 = +x, 2 = -z, 3 = -x)
    int b = 0;
    const float y = me.yaw;
    if (y >= 0.7853981852531433f && y < 2.356194496154785f) b = 1;
    else if (y >= 2.356194496154785f || y < -2.356194496154785f) b = 2;
    else if (y >= -2.356194496154785f && y < -0.7853981852531433f) b = 3;
    int w[4] = {40, 20, 5, 20};
    // the other roamers within 210 units
    std::vector<std::array<float, 3>> others;
    for (size_t j = 0; j < all.size(); ++j) {
        if (j == self || !all[j].roamer || !all[j].present) continue;
        if (dist3(all[j].pos, me.pos) > 210.0f) continue;
        others.push_back({all[j].pos[0] - me.pos[0], all[j].pos[1] - me.pos[1], all[j].pos[2] - me.pos[2]});
    }
    const CellInfo here = cell_at(t, me.pos);
    for (int k = 0; k < 4; ++k) {
        const float* d = kDirs[(b + k) & 3];
        const float tp[3] = {me.pos[0] + d[0], me.pos[1] + d[1], me.pos[2] + d[2]};
        const int base = w[k];
        w[k] = 0;
        bool blocked = false;
        for (const auto& o : others) {
            const int dot = int(o[0] * d[0] + o[1] * d[1] + o[2] * d[2]);
            if (dot > 0) { blocked = true; break; }
        }
        if (blocked) continue;
        const CellInfo target = cell_at(t, tp);
        if (!target.area) continue;   // (inside the area's box only)
        if (!target.part) {
            w[k] = std::max(1, base / 5);   // open ground: allowed, rare
        } else if (target.kind == 1 || (here.part && here.kind != 3 && target.kind == 3)) {
            w[k] = base;   // road, or a bridge entered from ground
        }
    }
    int total = 15;
    for (int k = 0; k < 4; ++k) total += w[k];
    int cum = 0;
    const int r = int(uint32_t(rng.next()) % 100u);
    for (int k = 0; k < 4; ++k) {
        cum += w[k] * 100 / total;
        if (cum > r) {
            const float* d = kDirs[(b + k) & 3];
            me.command = 1;
            me.target[0] = me.pos[0] + d[0];
            me.target[1] = me.pos[1] + d[1];
            me.target[2] = me.pos[2] + d[2];
            return;
        }
    }
    me.command = 2;
    me.wait = 60;
}

void roam_step(Roamer& r) {
    r.moving = false;
    if (r.command == 2) {
        if (--r.wait < 0) r.command = 0;
        return;
    }
    if (r.command != 1) return;
    float v[3] = {r.target[0] - r.pos[0], 0.0f, r.target[2] - r.pos[2]};
    const float len = std::sqrt(v[0] * v[0] + v[2] * v[2]);
    if (len <= r.speed) {   // arrived: snap onto the target
        r.pos[0] = r.target[0];
        r.pos[2] = r.target[2];
        r.command = 0;
        return;
    }
    v[0] = v[0] / len * r.speed;
    v[2] = v[2] / len * r.speed;
    r.pos[0] += v[0];
    r.pos[2] += v[2];
    r.yaw = angle_interpolate(r.yaw, std::atan2(v[0], v[2]), 0.1f, 0);
    r.moving = true;
}

}  // namespace dccore
