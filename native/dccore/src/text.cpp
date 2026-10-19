#include "dccore/text.h"
#include <cstring>

namespace dccore {

bool decode_glyph(uint16_t c, std::string& out) {
    if (c >= 0xfd21 && c <= 0xfd3a) { out += char('A' + (c - 0xfd21)); return true; }
    if (c >= 0xfd3b && c <= 0xfd54) { out += char('a' + (c - 0xfd3b)); return true; }
    if (c >= 0xfd6f && c <= 0xfd78) { out += char('0' + (c - 0xfd6f)); return true; }
    switch (c) {
        case 0xfd1f: out += '['; return true;
        case 0xfd20: out += ']'; return true;
        case 0xfd55: out += '\''; return true;
        case 0xfd57: out += '"'; return true;
        case 0xfd58: out += '!'; return true;
        case 0xfd59: out += '?'; return true;
        case 0xfd5a: out += '('; return true;
        case 0xfd5b: out += '*'; return true;
        case 0xfd5d: out += '-'; return true;
        case 0xfd5f: out += '&'; return true;
        case 0xfd60: out += '%'; return true;
        case 0xfd61: out += '('; return true;
        case 0xfd62: out += ')'; return true;
        case 0xfd6b: out += ':'; return true;
        case 0xfd6c: out += ','; return true;
        case 0xfd6d: out += '.'; return true;
        case 0xfd6e: out += '/'; return true;
        case 0xff02: out += ' '; return true;
        default: break;
    }
    if (c >= 0xfd00 && c < 0xff00) { out += '?'; return true; }
    return false;
}

MessageBank MessageBank::parse(View data) {
    MessageBank b;
    const size_t n = data.size() / 2;
    b.codes_.resize(n);
    std::memcpy(b.codes_.data(), data.data(), n * 2);
    if (n < 2) return b;
    const size_t table = size_t(uint16_t(b.codes_[0]));
    for (size_t i = 0; 2 + 2 * i + 1 < n && i < table; ++i) {   // short[0] is the number of {id, offset} pairs
        const int id = b.codes_[2 + 2 * i];
        const size_t start = table + 1 + size_t(uint16_t(b.codes_[3 + 2 * i]));
        if (start < n && !b.index_.count(id)) b.index_[id] = start;
    }
    return b;
}

std::string MessageBank::get(int id, const std::vector<std::string>& names) const {
    auto it = index_.find(id);
    if (it == index_.end()) return {};
    std::string out;
    for (size_t p = it->second; p < codes_.size(); ++p) {
        const uint16_t c = uint16_t(codes_[p]);
        if (c == 0xff01) break;
        if (c == 0xff00) { out += '\n'; continue; }
        if (c == 0xff03) { out += '\n'; continue; }
        if (c >= 0xfafa && c <= 0xfaff) {
            const size_t k = c - 0xfafa;
            out += k < names.size() ? names[k] : "?";
            continue;
        }
        decode_glyph(c, out);
    }
    return out;
}

std::vector<uint16_t> MessageBank::raw(int id) const {
    std::vector<uint16_t> out;
    auto it = index_.find(id);
    if (it == index_.end()) return out;
    for (size_t p = it->second; p < codes_.size(); ++p) {
        out.push_back(uint16_t(codes_[p]));
        if (uint16_t(codes_[p]) == 0xff01) break;
    }
    return out;
}

std::vector<MesToken> mes_tokens(const MessageBank& bank, int id) {
    std::vector<MesToken> out;
    for (uint16_t c : bank.raw(id)) {
        MesToken t;
        if (c == 0xff01) break;
        if (c == 0xff00) t.kind = MesToken::LineFeed;
        else if (c == 0xff02) { t.kind = MesToken::Space; t.text = " "; }
        else if (c == 0xff03) t.kind = MesToken::PageBreak;
        else if (c >= 0xfafa && c <= 0xfaff) { t.kind = MesToken::HeroName; t.arg = c - 0xfafa; }
        else if (c == 0xfbfe) { t.kind = MesToken::SysSlot; t.arg = 0; }
        else if (c == 0xfbfd) { t.kind = MesToken::SysSlot; t.arg = 1; }
        else if (c == 0xfbfc) { t.kind = MesToken::SysSlot; t.arg = 2; }
        else if (c == 0xfbfb) { t.kind = MesToken::SysSlot; t.arg = 3; }
        else if (c >= 0xfbed && c <= 0xfbf2) { t.kind = MesToken::SysSlot; t.arg = 4 + (0xfbf2 - c); }
        else if (c == 0xfbff) t.kind = MesToken::Number;
        else if (c >= 0xfbf3 && c <= 0xfbfa) { t.kind = MesToken::ValueSlot; t.arg = 0xfbfa - c; }
        else if ((c & 0xff00) == 0xfc00) { t.kind = MesToken::Colour; t.arg = c & 0xff; }
        else if ((c & 0xff00) == 0xf900) { t.kind = MesToken::Skip; t.arg = c & 0xff; }
        else if ((c & 0xff00) == 0xfe00) { t.kind = MesToken::Pause; t.arg = c & 0xff; }
        else if ((c & 0xff00) == 0xf600) { t.kind = MesToken::Shake; t.arg = c & 0xff; }
        else if (decode_glyph(c, t.text)) t.kind = MesToken::Glyph;
        else continue;
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<int> MessageBank::ids() const {
    std::vector<int> r;
    for (const auto& kv : index_) r.push_back(kv.first);
    return r;
}

std::string item_text_id_name(const MessageBank& system, int item_id) {
    const int id = item_id >= 0x101 ? item_id - 156 : item_id + 100;
    return system.get(id);
}

}  // namespace dccore
