// Prints the animated world matrices of every joint of a character at a given motion time, for comparison with the running game.
// Usage: dcpose <iso> <pack path on disc, e.g. dun/mainchara/c01d.chr> <time>   (model/motion/bbp names are taken from base.cfg)
#include <cstdio>
#include <cstdlib>
#include "dccore/character.h"
#include "dccore/disc.h"
#include "dccore/motion.h"
#include "dccore/pack.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: dcpose <iso> <pack> <time>\n"); return 2; }
    try {
        Disc disc(argv[1]);
        Pack pack(disc.read(argv[2]));
        double t = std::atof(argv[3]);
        View cfg = pack.get("base.cfg");
        auto motion_lines = cfg_strings(cfg, "MOTION");  // first MOTION line: three quoted names (.mot .bbp .wgt); cfg_strings gives only the first string
        std::string mdsn = cfg_strings(cfg, "MODEL").front();
        std::string stem = mdsn.substr(0, mdsn.size() - 4);
        Mds mds = parse_mds(pack.get(mdsn));
        auto bbp = parse_bbp(pack.get(stem + ".bbp"));
        auto tracks = parse_mot(pack.get(stem + ".mot"));
        Skeleton sk = make_skeleton(mds, bbp);
        Pose p = evaluate_pose(sk, tracks, t);
        std::printf("joints %zu tracks %zu\n", sk.size(), tracks.size());
        for (size_t i = 0; i < sk.size(); ++i) {
            std::printf("%zu", i);
            for (int k = 0; k < 16; ++k) std::printf(" %.6f", p.world[i][k]);
            for (int k = 0; k < 16; ++k) std::printf(" %.6f", p.local[i][k]);
            std::printf("\n");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
