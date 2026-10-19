// Sound data formats: PS-ADPCM, HD/BD banks, SCEIMidi sequences, .snd packs, SE tables. Port of tools/dc/snd.py sections 1-4 and 7 (see notes/sound.md).
#include "dccore/sound.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace dccore::snd {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ends_with(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && lower(s.substr(s.size() - n)) == suffix;
}

bool magic(View d, size_t off, const char* m8) { return off + 8 <= d.size() && std::memcmp(d.data() + off, m8, 8) == 0; }

// Sony table chunk: magic[8] u32 size u32 maxIndex u32 offset[maxIndex+1] (relative to the chunk; 0xFFFFFFFF = unused slot).
std::vector<uint32_t> chunk_offsets(View d, size_t off, const char* m8, const char* what) {
    if (!magic(d, off, m8)) throw Error(std::string("sound bank: missing chunk ") + what);
    const uint32_t mx = rd<uint32_t>(d, off + 12);
    std::vector<uint32_t> offs;
    if (mx == 0xFFFFFFFFu) return offs;
    if (mx > 0xFFFF) throw Error(std::string("sound bank: bad chunk size ") + what);
    offs.resize(size_t(mx) + 1);
    for (size_t i = 0; i < offs.size(); ++i) offs[i] = rd<uint32_t>(d, off + 16 + i * 4);
    return offs;
}

std::vector<std::string> split_lines(View v) {
    std::vector<std::string> out;
    std::string cur;
    for (uint8_t c : v) {
        if (c == '\r' || c == '\n') { out.push_back(cur); cur.clear(); }
        else cur += char(c);
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t j = i;
        while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
        if (j > i) t.push_back(s.substr(i, j - i));
        i = j;
    }
    return t;
}

bool is_int(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// "KEYWORD <int> , <int>" at the start of a line (leading blanks allowed)
bool parse_pair(const std::string& line, const char* kw, int& a, int& b) {
    size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    const size_t n = std::strlen(kw);
    if (line.compare(i, n, kw) != 0) return false;
    i += n;
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return false;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    auto num = [&](int& out) {
        size_t j = i;
        if (j < line.size() && line[j] == '-') ++j;
        const size_t d0 = j;
        while (j < line.size() && std::isdigit(static_cast<unsigned char>(line[j]))) ++j;
        if (j == d0) return false;
        out = std::atoi(line.substr(i, j - i).c_str());
        i = j;
        return true;
    };
    if (!num(a)) return false;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i >= line.size() || line[i] != ',') return false;
    ++i;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    return num(b);
}

int pk(int prog, int key) { return ((prog & 0xFFFF) << 16) | (key & 0xFFFF); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// PS-ADPCM
// ---------------------------------------------------------------------------------------------------------------------------------------------------
Pcm adpcm_decode(View bd, size_t start) {
    static const int F[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
    Pcm p;
    p.truncated = true;
    const size_t n = start < bd.size() ? (bd.size() - start) / 16 : 0;
    int s1 = 0, s2 = 0;
    bool have_loop = false;
    for (size_t b = 0; b < n; ++b) {
        const uint8_t* blk = bd.data() + start + b * 16;
        const int hdr = blk[0], flags = blk[1];
        int shift = hdr & 15;
        const int pred = std::min(hdr >> 4, 4);
        if (shift > 12) shift = 9;
        const int f0 = F[pred][0], f1 = F[pred][1];
        if ((flags & 4) && !have_loop) { p.loop_start = int32_t(b * 28); have_loop = true; }
        for (int i = 0; i < 14; ++i) {
            const int v = blk[2 + i];
            for (int h = 0; h < 2; ++h) {
                const int nib = h ? (v >> 4) : (v & 15);
                int x = (((nib ^ 8) - 8) * 4096) >> shift;
                x += (s1 * f0 + s2 * f1) >> 6;
                x = std::clamp(x, -32768, 32767);
                p.data.push_back(int16_t(x));
                s2 = s1;
                s1 = x;
            }
        }
        if (flags & 1) {
            p.truncated = false;
            p.looping = (flags & 2) != 0;
            break;
        }
    }
    if (!p.looping) p.loop_start = have_loop ? p.loop_start : 0;
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Bank
// ---------------------------------------------------------------------------------------------------------------------------------------------------
std::shared_ptr<Bank> Bank::parse(View hd, View bd, std::string name) {
    auto b = std::make_shared<Bank>();
    b->name = std::move(name);
    if (hd.empty()) return b;
    b->empty = false;
    if (!magic(hd, 0, "IECSsreV")) throw Error(b->name + ": no SCEIVers");
    if (!magic(hd, 0x10, "IECSdaeH")) throw Error(b->name + ": no SCEIHead");
    const uint32_t o_prog = rd<uint32_t>(hd, 0x24), o_sset = rd<uint32_t>(hd, 0x28), o_smpl = rd<uint32_t>(hd, 0x2C), o_vagi = rd<uint32_t>(hd, 0x30);

    // VAG info (8 bytes: u32 offset, u16 rate, u8 flags, u8 0xFF)
    {
        const auto offs = chunk_offsets(hd, o_vagi, "IECSigaV", "Vagi");
        b->vagi.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            if (offs[i] == 0xFFFFFFFFu) continue;
            const size_t a = o_vagi + offs[i];
            VagInfo& v = b->vagi[i];
            v.offset = rd<uint32_t>(hd, a);
            v.rate = rd<uint16_t>(hd, a + 4);
            v.flags = rd<uint8_t>(hd, a + 6);
            v.valid = true;
        }
    }
    // samples (42 bytes)
    {
        const auto offs = chunk_offsets(hd, o_smpl, "IECSlpmS", "Smpl");
        b->smpl.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            if (offs[i] == 0xFFFFFFFFu) continue;
            const size_t a = o_smpl + offs[i];
            View r = sub(hd, a, 42);
            Smpl& s = b->smpl[i];
            s.valid = true;
            s.vag = rd<uint16_t>(r, 0);
            s.vel_lo = r[2] & 0x7F;
            s.vel_hi = r[4] & 0x7F;
            s.base_key = r[11];
            s.fine = int8_t(r[12]);
            s.pan = r[13];
            s.group = r[14];
            s.priority = r[15];
            s.volume = r[16];
            s.adsr1 = rd<uint16_t>(r, 18);
            s.adsr2 = rd<uint16_t>(r, 20);
            s.spu_flags = r[41];
        }
    }
    // sample sets
    {
        const auto offs = chunk_offsets(hd, o_sset, "IECStesS", "Sset");
        b->sset.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            if (offs[i] == 0xFFFFFFFFu) continue;
            const size_t a = o_sset + offs[i];
            Sset& s = b->sset[i];
            s.valid = true;
            s.vel_curve = rd<uint8_t>(hd, a);
            s.vel_lo = rd<uint8_t>(hd, a + 1);
            s.vel_hi = rd<uint8_t>(hd, a + 2);
            const int n = rd<uint8_t>(hd, a + 3);
            for (int k = 0; k < n; ++k) s.smpl.push_back(rd<uint16_t>(hd, a + 4 + size_t(k) * 2));
        }
    }
    // programs
    {
        const auto offs = chunk_offsets(hd, o_prog, "IECSgorP", "Prog");
        b->prog.resize(offs.size());
        for (size_t i = 0; i < offs.size(); ++i) {
            if (offs[i] == 0xFFFFFFFFu) continue;
            const size_t a = o_prog + offs[i];
            const uint32_t hsz = rd<uint32_t>(hd, a);
            const int nsplit = rd<uint8_t>(hd, a + 4), bsz = rd<uint8_t>(hd, a + 5);
            if (bsz != 20) throw Error(b->name + ": split block size");
            Program& p = b->prog[i];
            p.valid = true;
            p.vol = rd<uint8_t>(hd, a + 6);
            p.pan = rd<uint8_t>(hd, a + 7);
            p.transpose = rd<int8_t>(hd, a + 8);
            p.detune = rd<int8_t>(hd, a + 9);
            p.pan_key_amount = rd<int8_t>(hd, a + 10);
            p.pan_key_center = rd<uint8_t>(hd, a + 11);
            for (int k = 0; k < nsplit; ++k) {
                View s = sub(hd, a + hsz + size_t(k) * 20, 20);
                Split sp;
                sp.sset = rd<uint16_t>(s, 0);
                sp.lo = s[2] & 0x7F;
                sp.hi = s[4] & 0x7F;
                sp.bend_lo = rd<uint16_t>(s, 6);
                sp.bend_hi = rd<uint16_t>(s, 8);
                sp.pan_key_amount = int8_t(s[14]);
                sp.pan_key_center = s[15];
                sp.vol = s[16];
                sp.pan = s[17];
                sp.transpose = int8_t(s[18]);
                sp.detune = int8_t(s[19]);
                p.splits.push_back(sp);
            }
        }
    }
    // pre-decode every VAG (the real-time render path never decodes)
    b->pcm.resize(b->vagi.size());
    for (size_t i = 0; i < b->vagi.size(); ++i) {
        const VagInfo& v = b->vagi[i];
        if (!v.valid || v.offset == 0xFFFFFFFFu || v.offset >= bd.size()) continue;
        b->pcm[i] = adpcm_decode(bd, v.offset);
    }
    return b;
}

int Bank::resolve(int program, int key, int vel, Layer* out, int max) const {
    if (program < 0 || size_t(program) >= prog.size() || !prog[size_t(program)].valid) return 0;
    const Program& p = prog[size_t(program)];
    int n = 0;
    for (const Split& sp : p.splits) {
        if (key < sp.lo || key > sp.hi) continue;
        if (sp.sset >= sset.size() || !sset[sp.sset].valid) continue;
        const Sset& ss = sset[sp.sset];
        if (vel < ss.vel_lo || vel > ss.vel_hi) continue;
        for (uint16_t si : ss.smpl) {
            if (si >= smpl.size() || !smpl[si].valid) continue;
            const Smpl& sm = smpl[si];
            if (vel < sm.vel_lo || vel > sm.vel_hi) continue;
            if (sm.vag >= vagi.size() || !vagi[sm.vag].valid || vagi[sm.vag].offset == 0xFFFFFFFFu) continue;
            if (n >= max) return n;
            out[n++] = {&sp, &ss, &sm, &vagi[sm.vag], &pcm[sm.vag], &p};
        }
    }
    return n;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Song (.sq)
// ---------------------------------------------------------------------------------------------------------------------------------------------------
namespace {
uint32_t vlq(View d, size_t& p) {
    uint32_t v = 0;
    for (;;) {
        const uint8_t b = rd<uint8_t>(d, p++);
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
}
}  // namespace

// Compact MIDI coding of MODMIDI FUN_00000f78/9b8 (see snd.parse_events).
Song Song::parse(View d, std::string name, int index) {
    Song s;
    s.name = std::move(name);
    if (!magic(d, 0, "IECSsreV") || !magic(d, 0x10, "IECSuqeS")) throw Error(s.name + ": not an SQ");
    const uint32_t o_midi = rd<uint32_t>(d, 0x24);
    if (o_midi == 0xFFFFFFFFu || !magic(d, o_midi, "IECSidiM")) throw Error(s.name + ": no SCEIMidi");
    const uint32_t csize = rd<uint32_t>(d, o_midi + 8), count_m1 = rd<uint32_t>(d, o_midi + 12);
    const uint32_t n_songs = count_m1 + 1;
    if (n_songs > 4096 || uint32_t(index) >= n_songs) throw Error(s.name + ": bad song index");
    std::vector<uint32_t> offs(n_songs);
    for (uint32_t i = 0; i < n_songs; ++i) offs[i] = o_midi + rd<uint32_t>(d, o_midi + 0x10 + size_t(i) * 4);
    const size_t a = offs[size_t(index)];
    const uint32_t hdr = rd<uint32_t>(d, a);
    s.division = rd<uint16_t>(d, a + 4);
    size_t p = a + hdr;
    size_t end = size_t(o_midi) + csize;
    if (uint32_t(index) + 1 < n_songs) end = offs[size_t(index) + 1];
    const size_t limit = std::min(end, d.size());

    uint32_t t = 0;
    int status = 0;
    bool nodelta = false;
    t += vlq(d, p);
    while (p < limit) {
        const size_t pos = p;
        const uint8_t b = rd<uint8_t>(d, p++);
        int st, d1;
        if (b & 0x80) {
            if (b < 0xF0) status = b;
            st = b;
            d1 = rd<uint8_t>(d, p++);
        } else {
            if (status == 0) throw Error(s.name + ": running status without status");
            st = status;
            d1 = b;
        }
        if (st < 0xF0) {
            const int hi = st & 0xF0, ch = st & 0xF;
            status = st;
            if (hi == 0x90 || hi == 0xB0 || hi == 0xE0 || hi == 0xA0) {
                const int d2 = rd<uint8_t>(d, p++);
                nodelta = ((d1 | d2) & 0x80) != 0;
                d1 &= 0x7F;
                const int d2v = d2 & 0x7F;
                if (hi == 0x90) s.events.push_back({t, uint8_t(d2v ? EvOn : EvOff), uint8_t(ch), uint8_t(d1), uint8_t(d2v), 0});
                else if (hi == 0xB0) s.events.push_back({t, EvCc, uint8_t(ch), uint8_t(d1), uint8_t(d2v), 0});
                else if (hi == 0xE0) s.events.push_back({t, EvBend, uint8_t(ch), uint8_t(d1), uint8_t(d2v), 0});
                // 0xA0 poly pressure: ignored
            } else if (hi == 0x80 || hi == 0xC0 || hi == 0xD0) {
                nodelta = (d1 & 0x80) != 0;
                d1 &= 0x7F;
                if (hi == 0x80) s.events.push_back({t, EvOff, uint8_t(ch), uint8_t(d1), 0, 0});
                else if (hi == 0xC0) s.events.push_back({t, EvPc, uint8_t(ch), uint8_t(d1), 0, 0});
                // 0xD0 channel pressure: ignored
            } else {
                throw Error(s.name + ": bad status");
            }
        } else {
            nodelta = false;
            if (st == 0xFF) {
                const int typ = d1;
                const uint32_t ln = vlq(d, p);
                View body = sub(d, p, ln);
                p += ln;
                if (typ == 0x51 && ln == 3) {
                    s.events.push_back({t, EvTempo, 0, 0, 0, (uint32_t(body[0]) << 16) | (uint32_t(body[1]) << 8) | body[2]});
                } else if (typ == 0x2F) {
                    s.events.push_back({t, EvEnd, 0, 0, 0, 0});
                    return s;
                }
            } else if (st == 0xF0 || st == 0xF7) {
                --p;   // d1 was the first length byte
                const uint32_t ln = vlq(d, p);
                p += ln;
            } else {
                throw Error(s.name + ": bad status");
            }
        }
        (void)pos;
        if (nodelta) nodelta = false;
        else t += vlq(d, p);
    }
    throw Error(s.name + ": sequence ends without FF 2F");
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// SndPack
// ---------------------------------------------------------------------------------------------------------------------------------------------------
SndPack::SndPack(Bytes data, const std::string& stem) : pack_(std::move(data)) {
    auto find_ext = [&](const char* ext) -> const PackEntry* {
        if (const PackEntry* e = pack_.find(stem + ext)) return e;
        for (const auto& e : pack_.entries())
            if (ends_with(e.name, ext)) return &e;
        return nullptr;
    };
    if (const PackEntry* e = find_ext(".txt")) {
        for (const std::string& l : split_lines(pack_.data(*e))) {
            const auto t = split_ws(l);
            if (!t.empty()) load_list.push_back(t[0]);   // names have no blanks
        }
    }
    if (const PackEntry* e = find_ext(".cfg")) {
        for (const std::string& l : split_lines(pack_.data(*e))) {
            int a, b;
            if (parse_pair(l, "REVERBE", a, b)) reverb = std::make_pair(a, b);
            else if (parse_pair(l, "TABLE", a, b)) table = std::make_pair(a, b);
        }
    }
}

std::vector<std::pair<char, std::shared_ptr<Bank>>> SndPack::banks() const {
    std::vector<std::pair<char, std::shared_ptr<Bank>>> out;
    for (const std::string& n : load_list) {
        if (!ends_with(n, ".hd") || n.size() < 9) continue;
        const PackEntry* hd = pack_.find(n);
        if (!hd || hd->size == 0) continue;
        const PackEntry* bd = pack_.find(n.substr(0, n.size() - 3) + ".bd");
        View bdv = bd ? pack_.data(*bd) : View();
        auto bank = Bank::parse(pack_.data(*hd), bdv, n);
        const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(n[5])));
        bool replaced = false;
        for (auto& o : out)
            if (o.first == letter) { o.second = bank; replaced = true; }
        if (!replaced) out.emplace_back(letter, bank);
    }
    return out;
}

std::vector<SongEntry> SndPack::songs(const std::unordered_map<std::string, int>* volumes) const {
    std::vector<SongEntry> out;
    for (const std::string& n : load_list) {
        if (!ends_with(n, ".sq") || n.size() < 9) continue;
        const PackEntry* e = pack_.find(n);
        if (!e || e->size == 0) continue;
        int vol = 100;
        if (volumes) {
            auto it = volumes->find(lower(n));
            if (it != volumes->end()) vol = it->second;
        }
        out.push_back({static_cast<char>(std::tolower(static_cast<unsigned char>(n[5]))), n,
                       std::make_shared<const Song>(Song::parse(pack_.data(*e), n)), vol});
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------------------------------------------------------------------------------
SndTables SndTables::load(const Elf& elf, const Disc& disc) {
    SndTables t;
    auto ent = [&](uint32_t va, int n) {
        std::vector<SeEntry> v;
        v.resize(size_t(n));
        View raw = elf.at(va, size_t(n) * 6);
        for (int i = 0; i < n; ++i) {
            v[size_t(i)].prog = int8_t(raw[size_t(i) * 6]);
            v[size_t(i)].key = int8_t(raw[size_t(i) * 6 + 1]);
            v[size_t(i)].flag = int8_t(raw[size_t(i) * 6 + 2]);
            v[size_t(i)].port = int8_t(raw[size_t(i) * 6 + 3]);
        }
        return v;
    };
    auto table = [&](uint32_t va, int maxn) {
        SeTable tb;
        if (!va) return tb;
        tb.present = true;
        for (int i = 0; i < maxn; ++i) {
            View r;
            try { r = elf.at(va + uint32_t(i) * 6, 4); } catch (const Error&) { break; }
            if (int8_t(r[0]) == -128) break;
            tb.e.push_back({int8_t(r[0]), int8_t(r[1]), int8_t(r[2]), int8_t(r[3])});
        }
        return tb;
    };
    t.se_info = ent(0x25DFB0, 2800);
    for (int i = 0; i < 101; ++i) t.cap.push_back(table(elf.get<uint32_t>(0x265BA0 + uint32_t(i) * 4), 100));
    for (int i = 0; i < 11; ++i) t.voice.push_back(table(elf.get<uint32_t>(0x266580 + uint32_t(i) * 4), 100));
    for (int i = 0; i < 2; ++i) t.basic.push_back(table(elf.get<uint32_t>(0x2A1ED8 + uint32_t(i) * 4), 200));
    t.special = ent(0x2665B0, 64);

    const auto setbl = disc.read("sound/tbl/setbl.txt");
    const auto lines = split_lines(setbl);
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto tk = split_ws(lines[i]);
        if (tk.size() >= 3 && is_int(tk[0]) && is_int(tk[1]) && is_int(tk[2]))
            t.setbl_vol[pk(std::atoi(tk[0].c_str()), std::atoi(tk[1].c_str()))] = std::atoi(tk[2].c_str());
    }
    const auto sqtbl = disc.read("sound/tbl/sqtbl.txt");
    const auto sl = split_lines(sqtbl);
    for (size_t i = 1; i < sl.size(); ++i) {
        const auto tk = split_ws(sl[i]);
        if (tk.size() >= 2 && is_int(tk[1])) t.sq_vol[lower(tk[0])] = std::atoi(tk[1].c_str());
    }
    return t;
}

std::optional<SeEntry> SndTables::lookup(int id, int basic_no, int cap_no, int voice_set) const {
    auto pick = [&](const SeTable& tb, int i) -> std::optional<SeEntry> {
        if (i < 0 || size_t(i) >= tb.e.size()) return std::nullopt;
        if (tb.e[size_t(i)].prog < 0) return std::nullopt;
        return tb.e[size_t(i)];
    };
    if (id < 0 || id > 0xAEF) return std::nullopt;
    if (basic_no >= 0 && id >= 100 && id < 300) {
        if (size_t(basic_no) >= basic.size()) return std::nullopt;
        return pick(basic[size_t(basic_no)], id - 100);
    }
    if (id >= 300 && id < 400 && cap_no >= 0 && size_t(cap_no) < cap.size() && cap[size_t(cap_no)].present) return pick(cap[size_t(cap_no)], id - 300);
    if (id >= 400 && id < 500 && voice_set >= 0 && size_t(voice_set) < voice.size() && voice[size_t(voice_set)].present)
        return pick(voice[size_t(voice_set)], id - 400);
    if (size_t(id) >= se_info.size() || se_info[size_t(id)].prog < 0) return std::nullopt;
    return se_info[size_t(id)];
}

int SndTables::volume_of(int prog, int key) const {
    auto it = setbl_vol.find(pk(prog, key));
    return it == setbl_vol.end() ? 0x40 : it->second;
}

}  // namespace dccore::snd
