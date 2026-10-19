#include "dccore/savedata.h"

namespace dccore {

void SaveData::initialize() {
    raw.fill(0);
    set<int32_t>(0x1cc, -1);
    set<float>(kNowTime, 0.0f);
    for (int ch = 0; ch < 6; ++ch)
        for (int i = 0; i < 0x80; ++i) set<int16_t>(kElem + size_t(ch) * 0x100 + size_t(i) * 2, int16_t(-1));
    for (int town = 0; town < 6; ++town) {
        const size_t base = kGrd + size_t(town) * kGrdSize;
        for (int i = 0; i < 0x82; ++i) set<int16_t>(base + 0x2a8 + size_t(i) * 0x10, int16_t(-1));
    }
    store_status(UserStatus::new_game());   // CDngStatusData::Initialize (CStockItem::Initialize only zeroes its block)
}

bool SaveData::game_flag(int i) const {
    if (i < 0 || (i >> 5) >= 0x100) return false;
    return (get<uint32_t>(kGameFlags + size_t(i >> 5) * 4) & (1u << (i & 0x1f))) != 0;
}

void SaveData::set_game_flag(int i, bool on) {
    if (i < 0 || (i >> 5) >= 0x100) return;
    uint32_t w = get<uint32_t>(kGameFlags + size_t(i >> 5) * 4);
    w &= ~(1u << (i & 0x1f));
    if (on) w |= 1u << (i & 0x1f);
    set<uint32_t>(kGameFlags + size_t(i >> 5) * 4, w);
}

bool SaveData::bit_of(size_t base, int map, int bit) const {
    if (map < 0 || map > 0x4f || bit < 0 || (bit >> 5) >= 0x10) return false;
    return (get<uint32_t>(base + size_t(map) * 0x40 + size_t(bit >> 5) * 4) & (1u << (bit & 0x1f))) != 0;
}

void SaveData::set_bit_of(size_t base, int map, int bit, bool on) {
    if (map < 0 || map > 0x4f || bit < 0 || (bit >> 5) >= 0x10) return;
    const size_t at = base + size_t(map) * 0x40 + size_t(bit >> 5) * 4;
    uint32_t w = get<uint32_t>(at);
    w &= ~(1u << (bit & 0x1f));
    if (on) w |= 1u << (bit & 0x1f);
    set<uint32_t>(at, w);
}

int SaveData::visit_map(int map, int times) {
    if (map < 0 || map > 0x4f) return 0;
    int v = get<int16_t>(kVisits + size_t(map) * 2) + times;
    if (v > 9999) v = 9999;
    set<int16_t>(kVisits + size_t(map) * 2, int16_t(v));
    return v;
}

void SaveData::add_now_time(float dt) {
    const float old = now_time();
    float t = old + dt;
    set_now_time(t);
    if (old < 8.5f && double(t) >= 8.5) add_day(1);   // dawn
    t = now_time();
    if (12.0f <= t) t -= 12.0f;
    if (t < 0.0f) t += 12.0f;
    set_now_time(t);
}

void SaveData::set_day(int d) {
    if (d > 0x270e) d = 0x270e;
    if (d < 0) d = 0;
    set<int32_t>(kDay, d);
}

void SaveData::add_fishing_points(int d) {
    int v = fishing_points() + d;
    if (v > 9999) v = 9999;
    if (v < 0) v = 0;
    set<int32_t>(kFishingPoint, v);
}

int SaveData::parts_count(int town) const {
    if (town < 0 || town > 5) return 0;
    int i = 0;
    while (i < 0x82 && get<int16_t>(kGrd + size_t(town) * kGrdSize + 0x2a8 + size_t(i) * 0x10) >= 0) ++i;
    return i;
}

std::vector<SaveData::GrdPart> SaveData::town_parts(int town) const {
    std::vector<GrdPart> out;
    if (town < 0 || town > 5) return out;
    const size_t base = kGrd + size_t(town) * kGrdSize + 0x2a8;
    for (int i = 0; i < 0x82; ++i) {
        const size_t at = base + size_t(i) * 0x10;
        GrdPart p;
        p.part = get<int16_t>(at);
        if (p.part < 0) break;
        p.rot = get<int16_t>(at + 2);
        for (int k = 0; k < 3; ++k) p.pos[k] = get<float>(at + 4 + 4 * size_t(k));
        out.push_back(p);
    }
    return out;
}

void SaveData::set_town_parts(int town, const std::vector<GrdPart>& parts) {
    if (town < 0 || town > 5) return;
    const size_t base = kGrd + size_t(town) * kGrdSize + 0x2a8;
    for (int i = 0; i < 0x82; ++i) {
        const size_t at = base + size_t(i) * 0x10;
        if (size_t(i) < parts.size()) {
            set<int16_t>(at, parts[size_t(i)].part);
            set<int16_t>(at + 2, parts[size_t(i)].rot);
            for (int k = 0; k < 3; ++k) set<float>(at + 4 + 4 * size_t(k), parts[size_t(i)].pos[k]);
        } else {
            set<int16_t>(at, int16_t(-1));
            set<int16_t>(at + 2, 0);
            for (int k = 0; k < 3; ++k) set<float>(at + 4 + 4 * size_t(k), 0.0f);
        }
    }
}

}  // namespace dccore

namespace dccore {

namespace {
const char* const kDefaultNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};
// alphabet sheet cell of an ASCII character (-1: not on the sheet)
int name_cell(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return 26 + (c - 'a');
    if (c >= '0' && c <= '9') return 80 + (c - '0');
    switch (c) {
        case '=': return 53; case '"': return 54; case '!': return 55; case '?': return 56; case '#': return 57; case '&': return 58; case '+': return 59;
        case '-': return 60; case '*': return 61; case '/': return 62; case '%': return 63; case '~': return 64; case '(': return 65; case ')': return 66;
        case '@': return 67; case '|': return 69; case '<': return 70; case '>': return 71; case '{': return 72; case '}': return 73; case ':': return 93;
        case ',': return 94; case '.': return 95; case '$': return 96; default: return -1;
    }
}
char cell_name(int cell) {
    if (cell >= 0 && cell < 26) return char('A' + cell);
    if (cell >= 26 && cell < 52) return char('a' + (cell - 26));
    if (cell >= 80 && cell < 90) return char('0' + (cell - 80));
    for (int c = 33; c < 127; ++c) if (name_cell(char(c)) == cell) return char(c);
    return '?';
}
}  // namespace

std::string SaveData::hero_name(int ch) const {
    if (ch < 0 || ch > 5) return {};
    std::string out;
    for (int i = 0; i < 10; ++i) {
        const uint16_t code = get<uint16_t>(kCharaName + size_t(ch) * 0x40 + size_t(i) * 2);
        if (code == 0) break;
        out += code == 0xe6 ? ' ' : (code >= 0xa2 ? cell_name(code - 0xa2) : '?');
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? kDefaultNames[ch] : out;
}

void SaveData::set_hero_name(int ch, const std::string& name) {
    if (ch < 0 || ch > 5) return;
    uint8_t* p = raw.data() + kCharaName + size_t(ch) * 0x40;
    std::memset(p, 0, 0x40);
    int n = 0;
    for (char c : name) {
        if (n >= 10) break;
        uint16_t code = 0;
        if (c == ' ') code = 0xe6;
        else if (name_cell(c) >= 0) code = uint16_t(name_cell(c) + 0xa2);
        else continue;
        std::memcpy(p + size_t(n) * 2, &code, 2);
        ++n;
    }
}

}  // namespace dccore
