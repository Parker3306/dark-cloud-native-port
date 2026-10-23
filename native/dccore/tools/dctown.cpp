// dctown <iso> <map> [gdata]: loads a town / story scene and prints what it holds. With "gdata" the developer's complete layout (gdata0.edt) is placed.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dccore/town.h"
#include "dccore/collision.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: dctown <iso> <map> [gdata]\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        std::vector<PlacedPart> layout;
        if (argc > 3 && !std::strcmp(argv[3], "gdata")) layout = parse_gdata_edt(disc.read(std::string("gedit/") + argv[2] + "/gdata0.edt"));
        const Town t = load_town(disc, argv[2], layout);
        size_t tris = 0;
        for (const TownAsset& a : t.assets)
            for (const Surface& s : a.lod[0].surfaces) tris += s.indices.size() / 3;
        std::printf("%s: %zu assets, %zu instances (%zu placed), %zu events, %zu people points, %zu textures, %zu areas, %zu failed, %zu lod0 triangles\n", argv[2], t.assets.size(),
                    t.instances.size(), layout.size(), t.events.size(), t.people.size(), t.textures.size(), t.areas.size(), t.failed.size(), tris);
        for (const std::string& f : t.failed) std::printf("  failed: %s\n", f.c_str());
        int missing_tex = 0;
        for (const TownAsset& a : t.assets)
            for (const Surface& s : a.lod[0].surfaces)
                if (!s.texture.empty() && !t.textures.count(s.texture)) ++missing_tex;
        std::printf("  surfaces with missing texture: %d\n", missing_tex);
        for (const TownInstance& in : t.instances)
            if (std::getenv("DCTOWN_INST")) std::printf("  inst %s col %d (%zu polys) part %d at (%.0f %.0f %.0f) bbox (%.0f %.0f %.0f)-(%.0f %.0f %.0f) yaw %.2f\n", t.assets[size_t(in.asset)].name.c_str(), int(t.assets[size_t(in.asset)].has_col), t.assets[size_t(in.asset)].col.polys.size(), in.part_def, in.pos[0], in.pos[1], in.pos[2], in.bbox_min[0], in.bbox_min[1], in.bbox_min[2], in.bbox_max[0], in.bbox_max[1], in.bbox_max[2], in.yaw);
        if (const char* cn = std::getenv("DCTOWN_COL"))   // the collision polygons of an asset, world places of the first instance (normal y, centre)
            for (const TownInstance& in : t.instances)
                if (t.assets[size_t(in.asset)].name == cn)
                    for (const CCPoly& q : t.assets[size_t(in.asset)].col.polys)
                        std::printf("  poly attr %08x %08x %08x %08x n (%.2f %.2f %.2f) v0 (%.0f %.0f %.0f) v1 (%.0f %.0f %.0f) v2 (%.0f %.0f %.0f) centre (%.0f %.0f %.0f) y %.0f..%.0f\n", q.attr[0], q.attr[1], q.attr[2], q.attr[3], q.n[0], q.n[1], q.n[2], q.v[0][0], q.v[0][1], q.v[0][2], q.v[1][0], q.v[1][1], q.v[1][2], q.v[2][0], q.v[2][1], q.v[2][2], (q.v[0][0] + q.v[1][0] + q.v[2][0]) / 3 + in.pos[0], (q.v[0][1] + q.v[1][1] + q.v[2][1]) / 3 + in.pos[1], (q.v[0][2] + q.v[1][2] + q.v[2][2]) / 3 + in.pos[2], std::min({q.v[0][1], q.v[1][1], q.v[2][1]}) + in.pos[1], std::max({q.v[0][1], q.v[1][1], q.v[2][1]}) + in.pos[1]);
        if (const char* fp = std::getenv("DCTOWN_FLOOR")) {   // "x z": the heights of every polygon of town_polys_near that covers that point (top view)
            float px = 0, pz = 0;
            std::sscanf(fp, "%f %f", &px, &pz);
            for (const CCPoly& q : town_polys_near(t, px, 0.0f, pz)) {
                auto sgn = [&](int a, int b) { return (px - q.v[b][0]) * (q.v[a][2] - q.v[b][2]) - (q.v[a][0] - q.v[b][0]) * (pz - q.v[b][2]); };
                const float d1 = sgn(0, 1), d2 = sgn(1, 2), d3 = sgn(2, 0);
                const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
                if (neg && pos) continue;
                if (q.n[1] == 0.0f) continue;
                const float y = q.v[0][1] - (q.n[0] * (px - q.v[0][0]) + q.n[2] * (pz - q.v[0][2])) / q.n[1];
                std::printf("  covers (%.0f %.0f): y %.1f  n (%.2f %.2f %.2f)\n", px, pz, y, q.n[0], q.n[1], q.n[2]);
            }
        }
        if (const char* mv = std::getenv("DCTOWN_MOVE")) {   // "x y z vx vy vz [steps]": MoveCheck on the town's polygons, one step after the other
            float pos[3], vel[3];
            int steps = 1;
            std::sscanf(mv, "%f %f %f %f %f %f %d", &pos[0], &pos[1], &pos[2], &vel[0], &vel[1], &vel[2], &steps);
            for (int k = 0; k < steps; ++k) {
                std::vector<CCPoly> polys = town_polys_near(t, pos[0], pos[1], pos[2]);
                float out[3], v[3] = {vel[0], vel[1], vel[2]};
                MoveInfo info;
                const bool wall = move_check(pos, v, out, info, polys.data(), int(polys.size()), 0);
                std::printf("  step %d: pos (%.2f %.2f %.2f) -> (%.2f %.2f %.2f) wall %d grounded %d foot %d n.y %.2f polys %zu\n", k, pos[0], pos[1], pos[2], out[0], out[1], out[2], int(wall), info.grounded, info.foot_found, info.foot.n[1], polys.size());
                pos[0] = out[0]; pos[1] = out[1]; pos[2] = out[2];
            }
        }
        if (const char* an = std::getenv("DCTOWN_ASSET"))
            for (const TownAsset& a : t.assets)
                if (a.name == an)
                    for (const Surface& sf : a.lod[0].surfaces) {
                        double sum[4] = {0, 0, 0, 0};
                        auto it = t.textures.find(sf.texture);
                        size_t px = 0;
                        if (it != t.textures.end()) { px = it->second.rgba.size() / 4; for (size_t i = 0; i < px; ++i) for (int k = 0; k < 4; ++k) sum[k] += it->second.rgba[i * 4 + size_t(k)]; }
                        std::printf("  surface tex '%s' tris %zu avg %.0f %.0f %.0f %.0f\n", sf.texture.c_str(), sf.indices.size() / 3, px ? sum[0] / double(px) : -1, px ? sum[1] / double(px) : -1, px ? sum[2] / double(px) : -1, px ? sum[3] / double(px) : -1);
                    }
        for (const auto* set : {&t.skies, &t.suns})
            for (const auto& kv : *set)
                for (const Surface& sf : kv.second.surfaces)
                    std::printf("  %s %d texture '%s' %s tris %zu" "\n", set == &t.skies ? "sky" : "sun", kv.first, sf.texture.c_str(), t.textures.count(sf.texture) ? "ok" : "MISSING", sf.indices.size() / 3);
        for (const TownEvent& e : t.events)
            if (e.kind == TownEvent::MapJump) std::printf("  jump %s id %d at (%.0f %.0f %.0f) yaw %.2f extent %.0f ev %d\n", e.map.c_str(), e.jump_id, e.pos[0], e.pos[1], e.pos[2], e.yaw, e.extent, e.event_no);
        for (const TownPerson& p : t.people) std::printf("  person %d at (%.0f %.0f %.0f)\n", p.villager, p.pos[0], p.pos[1], p.pos[2]);
        auto polys = town_polys_near(t, 0, 0, 0);
        std::printf("  polys near origin: %zu\n", polys.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
