// dctown <iso> <map> [gdata]: loads a town / story scene and prints what it holds. With "gdata" the developer's complete layout (gdata0.edt) is placed.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "dccore/town.h"

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
            if (std::getenv("DCTOWN_INST")) std::printf("  inst %s part %d at (%.0f %.0f %.0f) bbox (%.0f %.0f %.0f)-(%.0f %.0f %.0f) yaw %.2f\n", t.assets[size_t(in.asset)].name.c_str(), in.part_def, in.pos[0], in.pos[1], in.pos[2], in.bbox_min[0], in.bbox_min[1], in.bbox_min[2], in.bbox_max[0], in.bbox_max[1], in.bbox_max[2], in.yaw);
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
