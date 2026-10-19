#include "dccore/wgt.h"

namespace dccore {

std::vector<WgtRecord> parse_wgt(View d) {
    std::vector<WgtRecord> recs;
    size_t pos = 0;
    while (pos + 0x20 <= d.size()) {
        WgtRecord r;
        r.target = rd<uint32_t>(d, pos);
        r.bone = rd<uint32_t>(d, pos + 4);
        r.type = rd<uint32_t>(d, pos + 8);
        r.unk3 = rd<uint32_t>(d, pos + 12);
        uint32_t count = rd<uint32_t>(d, pos + 16);
        r.more = rd<uint32_t>(d, pos + 20);
        pos += 0x20;
        for (uint32_t i = 0; i < count; ++i) {
            r.entries.push_back({rd<uint32_t>(d, pos), rd<float>(d, pos + 0x10)});
            pos += 0x20;
        }
        bool more = r.more != 0;
        recs.push_back(std::move(r));
        if (!more) break;
    }
    return recs;
}

std::map<uint32_t, std::map<uint32_t, float>> vertex_weights(const std::vector<WgtRecord>& recs) {
    std::map<uint32_t, std::map<uint32_t, float>> out;
    for (const auto& r : recs)
        for (const auto& e : r.entries) out[e.vertex][r.bone] = e.weight;
    return out;
}

}  // namespace dccore
