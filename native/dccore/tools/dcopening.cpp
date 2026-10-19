// dcopening <iso>: runs the opening script with the game's timing and lists every camera cut with its start step; checks the camera files.
#include <cstdio>
#include "dccore/disc.h"
#include "dccore/opening.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dcopening <iso>\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const Bytes script = disc.read("opdat/opening.scr");
        const Bytes title = disc.read_root("TITLE.BIN");
        const auto cmds = parse_opening_script(std::string(script.begin(), script.end()));
        const auto names = opening_sne_names(title);
        std::printf("%zu commands, %zu camera files\n", cmds.size(), names.size());
        OpeningSim sim(cmds, names);
        int last_cut = -1;
        int waits = 0;
        int bad = 0;
        std::unique_ptr<SneClip> clip;
        while (!sim.finished() && sim.steps < 200000) {
            sim.step();
            if (sim.waiting_key() && ++waits >= 2) { sim.press_ok(); waits = 0; }
            if (sim.cut != last_cut) {
                last_cut = sim.cut;
                const std::string file = sim.clip_name();
                std::string rel = file;
                float p[3], r[3], fov = 0;
                bool ok = false;
                try {
                    clip = std::make_unique<SneClip>(load_sne(disc.read(rel), rel));
                    ok = clip->camera_at(sim.cam_start, p, r, &fov);
                } catch (const std::exception& e) { std::printf("   error %s\n", e.what()); }
                if (!ok) ++bad;
                std::printf("step %6ld  %5.1fs  cut %3d %-26s key %3d frames %g..%g speed %g  cam %s (%.0f,%.0f,%.0f) fov %.1f\n", sim.steps, double(sim.steps) / 60.0, sim.cut, file.c_str(), sim.cam_key,
                            sim.cam_start, sim.cam_end, sim.cam_speed, ok ? "ok" : "MISSING", ok ? p[0] : 0, ok ? p[1] : 0, ok ? p[2] : 0, double(fov));
            }
        }
        std::printf("finished after %ld steps (%.1f s), %d clips without a camera\n", sim.steps, double(sim.steps) / 60.0, bad);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
