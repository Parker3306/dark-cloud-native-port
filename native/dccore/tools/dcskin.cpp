// Prints the skinned (deformed) positions of the skin object at a motion time, for comparison with the running game's vertex buffer.
// Usage: dcskin <iso> <pack on disc> <time> [skin object name=skin]
#include <cstdio>
#include <cstdlib>
#include "dccore/character.h"
#include "dccore/disc.h"
#include "dccore/motion.h"
#include "dccore/pack.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: dcskin <iso> <pack> <time> [skin object]\n"); return 2; }
    try {
        Disc disc(argv[1]);
        Pack pack(disc.read(argv[2]));
        double t = std::atof(argv[3]);
        std::string skin_name = argc > 4 ? argv[4] : "skin";
        std::string mdsn = cfg_strings(pack.get("base.cfg"), "MODEL").front();
        std::string stem = mdsn.substr(0, mdsn.size() - 4);
        Mds mds = parse_mds(pack.get(mdsn));
        Skeleton sk = make_skeleton(mds, parse_bbp(pack.get(stem + ".bbp")));
        auto tracks = parse_mot(pack.get(stem + ".mot"));
        auto weights = vertex_weights(parse_wgt(pack.get(stem + ".wgt")));
        Pose pose = evaluate_pose(sk, tracks, t);
        const Object* skin = nullptr;
        for (const auto& o : mds.objects) if (o.name == skin_name) skin = &o;
        if (!skin) throw Error("skin object not found");
        int root = argc > 5 ? std::atoi(argv[5]) : skin->parent;
        std::vector<Mat4> mats = skin_matrices(sk, pose, root);
        std::printf("vertices %zu weighted %zu\n", skin->mesh.positions.size(), weights.size());
        for (size_t v = 0; v < skin->mesh.positions.size(); ++v) {
            const Vec4& p = skin->mesh.positions[v];
            Vec3d r{p.x, p.y, p.z};
            auto it = weights.find(uint32_t(v));
            Vec3d d = it == weights.end() ? r : skin_point(r, it->second, mats);
            std::printf("%zu %.6f %.6f %.6f\n", v, d.x, d.y, d.z);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
