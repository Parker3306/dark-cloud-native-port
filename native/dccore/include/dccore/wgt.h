#pragma once
// Skin-weight files (.wgt): a chain of records, each = 0x20-byte header + count * 0x20-byte entries. Port of tools/dc/wgt.py (read side).
// Each record blends the listed vertices of the target object toward one bone's transform by `weight` percent; records apply in order.
#include <map>
#include <vector>
#include "dccore/bytes.h"

namespace dccore {

struct WgtEntry {
    uint32_t vertex;
    float weight;  // percent (0..100)
};

struct WgtRecord {
    uint32_t target, bone, type, unk3, more;
    std::vector<WgtEntry> entries;
};

std::vector<WgtRecord> parse_wgt(View data);

// vertex -> { bone object index -> percent }. For the linear-blend files of this game each vertex's percents sum to 100.
std::map<uint32_t, std::map<uint32_t, float>> vertex_weights(const std::vector<WgtRecord>& recs);

}  // namespace dccore
