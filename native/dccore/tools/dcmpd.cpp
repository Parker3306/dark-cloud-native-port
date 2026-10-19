// Lists the model files of a pack on the disc and whether the C++ readers understand them: dcmpd <iso> <game path of the pack> [name filter]
#include <cstdio>
#include <cstring>
#include <string>
#include "dccore/disc.h"
#include "dccore/mds.h"
#include "dccore/pack.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: dcmpd <iso> <pack path> [filter]\n");
        return 2;
    }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        Pack pack(disc.read(argv[2]));
        const std::string filter = argc > 3 ? argv[3] : "";
        for (const auto& e : pack.entries()) {
            if (!filter.empty() && e.name.find(filter) == std::string::npos) continue;
            if (e.name.size() < 4 || e.name.compare(e.name.size() - 4, 4, ".mds") != 0) {
                std::printf("%-20s %8zu bytes\n", e.name.c_str(), pack.data(e).size());
                continue;
            }
            try {
                Mds m = parse_mds(pack.data(e), false);
                size_t meshes = 0, tris = 0;
                for (const auto& o : m.objects)
                    if (o.has_mesh) {
                        ++meshes;
                        for (const auto& g : o.mesh.groups) tris += triangulate(g, o.mesh.stride).size();
                    }
                std::printf("%-20s %8zu bytes  objects %zu meshes %zu triangles %zu\n", e.name.c_str(), pack.data(e).size(), m.objects.size(), meshes, tris);
            } catch (const std::exception& ex) {
                std::printf("%-20s %8zu bytes  ERROR %s\n", e.name.c_str(), pack.data(e).size(), ex.what());
            }
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    return 0;
}
