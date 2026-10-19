#pragma once
// Dark Cloud model container (.mds): objects (joints and parts) with optional meshes. Port of tools/dc/mds.py (read side only).
// Layout details: see the docstring of tools/dc/mds.py. Game space: Y up, +Z = front of a character, right-handed.
#include <array>
#include <string>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

struct Vec4 {
    float x, y, z, w;
};

struct Group {
    int32_t type;      // 3 = triangle list, 4 = triangle strip
    int32_t material;  // index into Mesh::materials (-1 = keep the previous material)
    std::vector<int32_t> tuples;  // flat: `stride` ints per vertex tuple (position, normal, uv[, colour]) indices
};

struct Mesh {
    std::vector<Vec4> positions, normals, uvs, colors;
    std::vector<Group> groups;
    std::vector<Bytes> materials;  // raw 0x60-byte records
    int stride = 3;                // ints per vertex tuple: 3 (position, normal, uv) or 4 (+ colour)

    std::string texture_name(size_t material) const;  // name stored in a material record ("" if out of range)
};

struct Object {
    std::string name;
    int32_t parent = -1;
    std::array<float, 16> matrix{};  // local transform, row-vector convention, translation in the last row
    bool has_mesh = false;
    Mesh mesh;
};

struct Mds {
    uint32_t version = 0;
    std::vector<Object> objects;
};

// Throws Error on anything it does not understand (e.g. map collision/camera meshes). `strict` also checks that the blocks of a mesh do not overlap, which
// is what the byte-exact writer in tools/dc needs; map pieces with a colour block are stored in a different order and only read with strict = false.
Mds parse_mds(View data, bool strict = true);

// World matrices of every object (row-vector convention: world = local * parent_world), in double precision.
std::vector<std::array<double, 16>> world_matrices(const Mds& m);

struct Triangle {
    int32_t pos[3], nrm[3], uv[3], col[3];  // col = -1 when the mesh has no colour block
};
// Triangle list / strip -> triangles; degenerate ones (a repeated position index) are dropped. Same order as tools/dc/export_model.py.
std::vector<Triangle> triangulate(const Group& g, int stride);

}  // namespace dccore
