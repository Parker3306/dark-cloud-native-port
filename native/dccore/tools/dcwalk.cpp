// Headless walk: dcwalk <iso> <stem> <seed> <rooms> <map_no> <script>
// script: a file with one line per tick "lx ly rx ry buttons" (raw pad bytes 0..255, buttons as a number), or "-" to hold the stick up for 300 ticks.
// Prints one line per tick: tick px py pz yaw | cam pos | cam ref | dist height angle | grounded motion
// With "--state <file>" before the script path the walker starts from the state in the file (written by tools/dc/validate_walk.py).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/floor.h"
#include "dccore/pack.h"
#include "dccore/walk.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: dcwalk <iso> <stem> <seed> <rooms> <map_no> [--init ...] <script|->\n");
        return 2;
    }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const std::string stem = argv[2];
        Bytes cfg = disc.read("dun/" + stem + ".cfg");
        Pack pack(disc.read("dun/mpd_pack/" + stem + ".mpd"));
        Dungeon dun = load_dungeon(std::string(cfg.begin(), cfg.end()), pack);
        FloorLayout layout = build_random_map(uint32_t(std::strtoul(argv[3], nullptr, 0)), std::atoi(argv[4]), std::atoi(argv[5]));
        Walker w(&dun, &layout);
        int a = 6;
        if (std::string(argv[a]) == "--state") {   // key v... lines: ppos pvy pyaw pdisp cpos cref cnext_pos cnext_ref cspeed cdist cheight cangle cangle_cur cfollow cangle_h
            std::ifstream f(argv[a + 1]);
            std::string line;
            while (std::getline(f, line)) {
                std::istringstream ss(line);
                std::string k;
                ss >> k;
                float v[3] = {0, 0, 0};
                ss >> v[0] >> v[1] >> v[2];
                auto set3 = [&](float* d) { d[0] = v[0]; d[1] = v[1]; d[2] = v[2]; };
                if (k == "ppos") set3(w.player.pos);
                else if (k == "pvy") w.player.vy = v[0];
                else if (k == "pyaw") w.player.yaw = v[0];
                else if (k == "pdisp") set3(w.player.disp);
                else if (k == "cpos") set3(w.camera.pos);
                else if (k == "cref") set3(w.camera.ref);
                else if (k == "cnext_pos") set3(w.camera.next_pos);
                else if (k == "cnext_ref") set3(w.camera.next_ref);
                else if (k == "cspeed") w.camera.speed = v[0];
                else if (k == "cdist") w.camera.dist = v[0];
                else if (k == "cheight") w.camera.height = v[0];
                else if (k == "cangle") w.camera.angle = v[0];
                else if (k == "cangle_cur") w.camera.angle_cur = v[0];
                else if (k == "cfollow") set3(w.camera.follow);
                else if (k == "cangle_h") w.camera.angle_h = v[0];
            }
            a += 2;
        } else {
            w.place_at_start();
        }
        std::vector<PadRaw> ticks;
        const std::string script = argv[a];
        if (script == "-") {
            for (int i = 0; i < 300; ++i) { PadRaw p; p.ly = 0; ticks.push_back(p); }
        } else {
            std::ifstream f(script);
            std::string line;
            while (std::getline(f, line)) {
                std::istringstream ss(line);
                int lx, ly, rx, ry;
                unsigned b;
                if (ss >> lx >> ly >> rx >> ry >> b) {
                    PadRaw p;
                    p.lx = uint8_t(lx); p.ly = uint8_t(ly); p.rx = uint8_t(rx); p.ry = uint8_t(ry); p.buttons = b;
                    ticks.push_back(p);
                }
            }
        }
        int t = 0;
        for (const PadRaw& p : ticks) {
            w.tick(p);
            std::printf("%d %.6f %.6f %.6f %.6f | %.5f %.5f %.5f | %.5f %.5f %.5f | %.5f %.5f %.5f | %d %d\n", t++, w.player.pos[0], w.player.pos[1], w.player.pos[2], w.player.yaw,
                        w.camera.pos[0], w.camera.pos[1], w.camera.pos[2], w.camera.ref[0], w.camera.ref[1], w.camera.ref[2], w.camera.dist, w.camera.height, w.camera.angle,
                        w.player.info.grounded, w.player.motion);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
