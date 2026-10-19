// dcgeorama <iso>: loads the five towns with the developer's complete layouts (gdata0.edt), marks every part obtained and every tip installed and prints the
// request bits (compare with tools/dc/georama_requests.py: e01 4 of 8, e02 10 of 10, e03 9 of 10, e04 9 of 9, e05 13 of 13), then removes and re-places a part.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "dccore/disc.h"
#include "dccore/edit.h"
#include "dccore/georama.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dcgeorama <iso>\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        Elf elf(disc.read_root("SCUS_971.11"));
        const GeoTables tables = GeoTables::load(elf);
        for (int n = 0; n < 5; ++n) {
            const std::string name = "e0" + std::to_string(n + 1);
            const auto layout = parse_gdata_edt(disc.read("gedit/" + name + "/gdata0.edt"));
            Town t = load_town(disc, name, layout);
            SaveData save;
            Georama g(t, save, tables, n);
            save.set_game_int(0, 100);
            for (int p = 0; p < 24; ++p) {
                georama_atra_get(save, tables, n, p);
                g.set_comp_event(p, true);
                for (int k = 0; k < 6; ++k) if (tables.parts[n][p].elem[k].id >= 0) g.set_installed(p, k, true);
            }
            for (const WaterSurfaceDef& w : t.info.water_surfaces)
                std::printf("   water %s nx %d nz %d p0 %.1f,%.1f,%.1f p1 %.1f,%.1f,%.1f origin %.1f,%.1f,%.1f params %.3f %.3f %.3f %.3f rgb %.0f,%.0f,%.0f extra %d,%d,%d shakes %zu\n", w.texture.c_str(), w.nx, w.nz, double(w.p0[0]), double(w.p0[1]), double(w.p0[2]), double(w.p1[0]), double(w.p1[1]), double(w.p1[2]), double(w.origin[0]), double(w.origin[1]), double(w.origin[2]), double(w.params[0]), double(w.params[1]), double(w.params[2]), double(w.params[3]), double(w.rgb[0]), double(w.rgb[1]), double(w.rgb[2]), w.extra[0], w.extra[1], w.extra[2], w.shakes.size());
            g.request_check();
            std::printf("%s: %d slots, requests met %d of %d:", name.c_str(), int(std::count_if(t.slots.begin(), t.slots.end(), [](const EditSlot& s) { return s.used; })), g.request_count(), g.request_total());
            for (int p = 0; p < 24; ++p) if (g.request(p)) std::printf(" %d", p);
            std::printf("\n");
            if (n == 1)
                for (const EditSlot& s : t.slots)
                    if (s.used && s.def == 14) {
                        const Town::Area& a = t.areas[size_t(s.area)];
                        std::printf("   part 14 at area %d cell %d,%d rot %d; types around:", s.area, s.cx, s.cz, s.rot);
                        for (int z = s.cz - 2; z <= s.cz + 2; ++z) { for (int x = s.cx - 3; x <= s.cx + 3; ++x) std::printf(" %2d", (x >= 0 && z >= 0 && x < a.def.width && z < a.def.height) ? int(a.part[size_t(z * a.def.width + x)]) : -9); std::printf(" |"); }
                        std::printf("%c", 10);
                    }
            // take the first house away and put it back
            for (const EditSlot& s : t.slots)
                if (s.used && s.def == 0) {
                    const float cursor[3] = {s.pos[0], s.pos[1], s.pos[2]};
                    const int rot = s.rot;
                    const int removed = g.remove_at(cursor);
                    std::string why;
                    const int back = g.place(0, rot, cursor, &why);
                    std::printf("   removed part %d, placed again: slot %d %s\n", removed, back, back < 0 ? why.c_str() : "");
                    break;
                }
            std::printf("   edit log: %zu changes\n", t.edit_log.size());
            {   // save the layout and load it again: the same parts must stand in the same places
                g.save_layout();
                const auto saved = Georama::saved_layout(save, n);
                Town t2 = load_town(disc, name, saved);
                int a = 0, b = 0, same = 0;
                for (const EditSlot& s : t.slots) if (s.used) ++a;
                for (const EditSlot& s : t2.slots) if (s.used) ++b;
                for (const EditSlot& s : t.slots)
                    if (s.used)
                        for (const EditSlot& r : t2.slots)
                            if (r.used && r.def == s.def && r.rot == s.rot && std::fabs(r.pos[0] - s.pos[0]) < 0.01f && std::fabs(r.pos[2] - s.pos[2]) < 0.01f) { ++same; break; }
                std::printf("   saved %zu records, reloaded %d of %d slots, %d identical\n", saved.size(), b, a, same);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
