// Dark Cloud sound engine: sequencer + SPU2-style synth + SoundSystem (the Snd* layer). Port of tools/dc/snd.py sections 5-7b, restructured for real time:
// voices are advanced sample by sample (SPU2 ADSR state machine, linear interpolation, per-channel controller state) instead of rendering whole notes.
#include "dccore/sound.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace dccore::snd {

namespace {

constexpr int kPorts = 16;
constexpr int kMaxVoices = 256;
constexpr int kMaxLayers = 32;
constexpr int kBlock = 256;                 // mixing segment (also the gain-ramp granularity)
constexpr int kFrameSamples = kRate / 60;   // the game's 60 Hz step
constexpr int kMaxSched = 32;

// hsyn ports (LoadHdBd_x): A 0 (player 0), E 1 (player 1), I 2 and 14, G 10, S 11, Q 12, M 13, C 15
int port_priority(int port) {
    switch (port) {
        case 0: return 0x20;
        case 1: return 0x60;
        case 2: return 0x50;
        case 10: return 0x40;
        case 11: return 0x90;
        case 12: return 0x90;
        case 13: return 0x80;
        case 14: return 0x40;
        case 15: return 0x70;
        default: return 0x40;
    }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// SPU2 ADSR as a per-sample state machine. Identical stage formulas to snd.adsr_envelope (closed form there): every stage starts with the previous
// stage's last level (so a stage change repeats one sample), levels are in 0..0x7FFF.
// ---------------------------------------------------------------------------------------------------------------------------------------------------
void rate_params(int rate7, bool increase, double& step, int& psh) {
    const int shift = rate7 >> 2, n = rate7 & 3;
    const int s = increase ? (7 - n) : (-8 + n);
    psh = std::max(0, shift - 11);
    step = double(s) * double(1u << std::max(0, 11 - shift));
}

struct Env {
    enum Stage : uint8_t { Attack, Attack2, Decay, Sustain, Release, Done };
    uint16_t adsr1 = 0, adsr2 = 0;
    Stage stage = Done;
    uint32_t k = 0, n_len = 0xFFFFFFFFu;
    double level0 = 0, last = 0, step = 0, mult = 1, r = 1, sl = 0, lv2 = 0;
    uint32_t u1 = 0;
    int psh = 0;
    bool att_exp = false, sus_dec = false, sus_exp = false, rel_exp = false;

    void start(uint16_t a1, uint16_t a2) {
        adsr1 = a1;
        adsr2 = a2;
        att_exp = (a1 & 0x8000) != 0;
        sl = double(((a1 & 15) + 1) * 0x800);
        sus_exp = (a2 & 0x8000) != 0;
        sus_dec = (a2 & 0x4000) != 0;
        rel_exp = (a2 & 0x20) != 0;
        last = 0;
        enter(Attack);
    }

    void enter(Stage s) {
        stage = s;
        k = 0;
        level0 = last;
        mult = 1;
        n_len = 0xFFFFFFFFu;
        switch (s) {
            case Attack: {
                rate_params((adsr1 >> 8) & 0x7F, true, step, psh);
                const double period = double(1u << psh);
                if (!att_exp) {
                    n_len = uint32_t(std::ceil(32767.0 / step) * period + 1.0);
                } else {
                    u1 = uint32_t(0x6000 / int(step)) + 1;
                    n_len = uint32_t(double(u1) * period);
                }
                break;
            }
            case Attack2: {
                const double period = double(1u << psh);
                lv2 = step * double(u1);
                n_len = lv2 < 32767.0 ? uint32_t(std::ceil((32767.0 - lv2) / step) * period * 4.0 + 1.0) : 1u;
                break;
            }
            case Decay: {
                if (!(last > sl)) { enter(Sustain); return; }
                rate_params(((adsr1 >> 4) & 15) * 4, false, step, psh);
                r = 1.0 + step / 32768.0;
                const double kmax = std::ceil(std::log(std::max(sl, 1.0) / last) / std::log(r));
                n_len = uint32_t(kmax * double(1u << psh) + 1.0);
                break;
            }
            case Sustain:
                rate_params((adsr2 >> 6) & 0x7F, !sus_dec, step, psh);
                r = 1.0 + step / 32768.0;
                break;
            case Release:
                if (!(last > 0)) { stage = Done; return; }
                rate_params((adsr2 & 31) * 4, false, step, psh);
                r = 1.0 + step / 32768.0;
                if (!rel_exp) n_len = uint32_t(std::ceil(last / -step) * double(1u << psh));
                break;
            case Done: break;
        }
    }

    void key_off() {
        if (stage == Release || stage == Done) return;
        enter(Release);
    }

    // Level of the next output sample in 0..1, or -1 when the envelope has finished.
    float next() {
        for (;;) {
            double lvl;
            switch (stage) {
                case Done: return -1.f;
                case Attack:
                    if (k >= n_len) { enter(att_exp ? Attack2 : Decay); continue; }
                    lvl = step * double(k >> psh);
                    if (!att_exp) lvl = std::min(lvl, 32767.0);
                    break;
                case Attack2:
                    if (k >= n_len) { enter(Decay); continue; }
                    lvl = std::min(lv2 + step * double(k >> (psh + 2)), 32767.0);
                    break;
                case Decay:
                    if (k >= n_len) { enter(Sustain); continue; }
                    if (k > 0 && (k & ((1u << psh) - 1)) == 0) mult *= r;
                    lvl = std::max(level0 * mult, sl);
                    break;
                case Sustain:
                    if (sus_dec) {
                        if (sus_exp) {
                            if (k > 0 && (k & ((1u << psh) - 1)) == 0) mult *= r;
                            lvl = level0 * mult;
                            if (lvl < 0.5) { stage = Done; return -1.f; }
                        } else {
                            lvl = std::max(level0 + step * double(k >> psh), 0.0);
                            if (lvl <= 0.0) { stage = Done; return -1.f; }
                        }
                    } else {
                        lvl = std::min(level0 + step * double(k >> psh), 32767.0);   // exponential increase: approximated linear (as snd.py)
                    }
                    break;
                case Release:
                    if (rel_exp) {
                        if (k > 0 && (k & ((1u << psh) - 1)) == 0) mult *= r;
                        lvl = level0 * mult;
                        if (lvl < 1.0) { stage = Done; return -1.f; }
                    } else {
                        if (k >= n_len) { stage = Done; return -1.f; }
                        lvl = std::max(level0 + step * double(k >> psh), 0.0);
                    }
                    break;
                default: return -1.f;
            }
            ++k;
            last = lvl;
            return float(lvl / 32767.0);
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Reverb stand-in: 4 feedback combs per side exactly as snd.reverb (delays/gains scaled by the preset), optionally followed by two allpasses.
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct Reverb {
    static constexpr int kMaxD = 4096;
    struct Comb { std::vector<float> buf; int d = 1, pos = 0; float g = 0; };
    struct AllPass { std::vector<float> buf; int d = 1, pos = 0; };
    Comb comb[2][4];
    AllPass ap[2][2];
    float wet = 0;
    int mode = 0, depth = 0;
    bool diffusion = true, presets = true;

    Reverb() {
        for (auto& c : comb) for (auto& x : c) x.buf.assign(kMaxD, 0.f);
        for (auto& c : ap) for (auto& x : c) x.buf.assign(kMaxD, 0.f);
        configure(4, 0);
    }
    void configure(int m, int dep) {
        mode = m;
        depth = dep;
        wet = float(dep) / 128.f;
        static const int base[4] = {1557, 1617, 1491, 1422};
        static const float gain[4] = {0.78f, 0.77f, 0.76f, 0.75f};
        static const int off[4] = {0, 23, 41, 59};
        const double sc = double(kRate) / 44100.0 * (!presets ? 1.0 : m <= 2 ? 0.55 : m == 3 ? 0.8 : 1.0);
        const float gsub = !presets ? 0.f : (m <= 2) ? 0.08f : (m == 3) ? 0.03f : 0.f;
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 4; ++i) {
                Comb& cb = comb[c][i];
                cb.d = std::clamp(int((base[i] + off[i] * c) * sc), 1, kMaxD);
                cb.g = gain[i] - gsub;
                std::fill(cb.buf.begin(), cb.buf.end(), 0.f);
                cb.pos = 0;
            }
            static const int apd[2] = {556, 441};
            for (int i = 0; i < 2; ++i) {
                AllPass& a = ap[c][i];
                a.d = std::clamp(int((apd[i] + 23 * c) * sc), 1, kMaxD);
                std::fill(a.buf.begin(), a.buf.end(), 0.f);
                a.pos = 0;
            }
        }
    }
    // adds the reverb return of the input to out
    void process(const float* in_l, const float* in_r, float* out_l, float* out_r, int n) {
        const float* in[2] = {in_l, in_r};
        float* out[2] = {out_l, out_r};
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < n; ++i) {
                const float x = in[c][i];
                float acc = 0;
                for (int j = 0; j < 4; ++j) {
                    Comb& cb = comb[c][j];
                    const float y = x + cb.g * cb.buf[size_t(cb.pos)] + 1e-20f;   // tiny offset keeps the feedback paths out of the denormal range
                    cb.buf[size_t(cb.pos)] = y;
                    if (++cb.pos >= cb.d) cb.pos = 0;
                    acc += y;
                }
                // snd.py's stand-in returns the comb sum including the direct input; with diffusion on the return is the pure wet tail (like the SPU2's)
                float o = (diffusion ? acc - 4.f * x : acc) * 0.25f * wet;
                if (diffusion) {
                    for (int j = 0; j < 2; ++j) {
                        AllPass& a = ap[c][j];
                        const float w_old = a.buf[size_t(a.pos)];   // Schroeder allpass (unity gain): w = x + g*w[n-d], y = w[n-d] - g*w
                        const float w = o + 0.5f * w_old;
                        a.buf[size_t(a.pos)] = w;
                        o = w_old - 0.5f * w;
                        if (++a.pos >= a.d) a.pos = 0;
                    }
                }
                out[c][i] += o;
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Sequencer state
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct Chan {
    uint8_t prog = 0, port = 0, vol = 127, expr = 127, pan = 64;
    uint16_t bend = 0x2000;
    bool pedal = false;
    uint8_t nfifo[128];
    uint32_t fifo[128][4];
    uint32_t pending[32];
    int npending = 0;
    void reset(int p) {
        prog = 0; port = uint8_t(p); vol = 127; expr = 127; pan = 64; bend = 0x2000; pedal = false;
        std::memset(nfifo, 0, sizeof nfifo);
        npending = 0;
    }
};

struct Fade { int mode = 0; float now = 0, target = 0, step = 0; bool stop = false; };

struct Player {
    int index = 0, port = 0;
    std::shared_ptr<const SongSet> songs;
    const Song* song = nullptr;
    bool playing = false, paused = false;
    size_t ev = 0;
    uint32_t last_tick = 0;
    double sec = 0, tempo = 500000.0, next_sec = 0;
    int64_t start_frame = 0, next_frame = 0, pause_frame = 0;
    int nrpn = -1, dentry = -1;
    struct Loop { size_t ev = 0; uint32_t tick = 0; int remaining = -1; bool valid = false; } loops[128];
    int endless_done[128];
    Chan ch[16];
    int vol = 0, default_vol = 0;
    float gain_cur = 0;
    Fade fade;
};

struct Voice {
    bool active = false, released = false;
    Env env;
    const Pcm* pcm = nullptr;
    int32_t n_src = 0, ls = 0;
    bool looping = false;
    double pos = 0;
    double base_ratio = 1;
    float amp = 0, gain = 1, pan_base = 64, pan_off = 0, bend_hi = 0, bend_lo = 0;
    uint8_t port = 0, core = 0, group = 2;
    bool send_l = false, send_r = false;
    Player* pl = nullptr;
    Chan* ch = nullptr;
    uint32_t note_id = 0;
    uint32_t handle = 0;
    int se_id = -1;
    int priority = 0;
    uint64_t age = 0;
    uint32_t k = 0, natural = 0, rel_n = 0;
};

enum CmdType : uint8_t {
    CSetBank, CSetSongs, CPlay, CStop, CPause, CResume, CSetVol, CFade, CLoopLimit, CPlayNote, CStopSe, CStopHandle, CStopAllSe, CSeVol, CSePan,
    CReverb, CMaster, CStereo, CReverbOn, CSendAll, CDiffusion, CLimiter, CVoiceLimit, CStopPort, CPresets
};

struct Cmd {
    CmdType type = CStop;
    int i[6] = {0, 0, 0, 0, 0, 0};
    float f[3] = {0, 0, 0};
    uint32_t u = 0;
    std::shared_ptr<Bank> bank;
    std::shared_ptr<const SongSet> songs;
};

struct Sched { uint32_t handle = 0; int left = 0; };

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Engine (audio-thread state)
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct Engine {
    std::array<std::shared_ptr<Bank>, kPorts> banks;
    Player players[2];
    std::vector<Voice> voices = std::vector<Voice>(kMaxVoices);
    Reverb rv[2];
    int64_t frame = 0;
    int tick_acc = kFrameSamples;
    uint64_t age = 0;
    uint32_t note_counter = 0;
    int loop_limit = -1;
    int voice_limit[2] = {24, 24};
    float master = 1.f;
    bool stereo = true, reverb_on = true, send_all = false, limiter = true;
    Sched sched[kMaxSched];
    int missing = 0, active = 0, peak = 0;
    // scratch
    float L[kBlock], R[kBlock], RIN[2][2][kBlock];

    Engine() {
        players[0].index = 0; players[0].port = 0;
        players[1].index = 1; players[1].port = 1;
    }

    // ---- voices ---------------------------------------------------------------------------------------------------------------------------------
    Voice* alloc_voice(int core, int priority) {
        Voice* free_v = nullptr;
        Voice* victim = nullptr;
        int count = 0;
        const int limit = std::min(voice_limit[core], kMaxVoices);
        for (Voice& v : voices) {
            if (!v.active) { if (!free_v) free_v = &v; continue; }
            if (v.core != core) continue;
            ++count;
            if (!victim || v.priority < victim->priority ||
                (v.priority == victim->priority && ((v.released && !victim->released) || (v.released == victim->released && v.age < victim->age))))
                victim = &v;
        }
        if (count < limit && free_v) return free_v;
        if (!victim) return free_v;
        if (priority < victim->priority) return nullptr;
        victim->active = false;
        return victim;
    }

    struct NoteReq {
        Player* pl = nullptr;
        Chan* ch = nullptr;
        int port = 0, prog = 0, key = 0, vel = 127;
        uint32_t note_id = 0, handle = 0;
        int se_id = -1;
        float gain = 1.f;
        float pan_off = 0.f;
    };

    void start_note(const NoteReq& n) {
        const Bank* bank = banks[size_t(n.port & 15)].get();
        if (!bank || bank->empty) { ++missing; return; }
        Bank::Layer lay[kMaxLayers];
        const int cnt = bank->resolve(n.prog, n.key, n.vel, lay, kMaxLayers);
        if (cnt == 0) { ++missing; return; }
        for (int li = 0; li < cnt; ++li) {
            const Bank::Layer& L_ = lay[li];
            const Pcm& pcm = *L_.pcm;
            if (pcm.data.empty()) continue;
            const Program& po = *L_.prog;
            const Split& sp = *L_.split;
            const Smpl& sm = *L_.smpl;
            const int sf = sm.spu_flags;
            const int core = (sf & 0x20) ? 1 : (sf & 0x10) ? 0 : (n.port == 0 ? 0 : 1);
            const int prio = sm.priority + port_priority(n.port);
            Voice* v = alloc_voice(core, prio);
            if (!v) continue;
            *v = Voice();
            v->active = true;
            v->age = ++age;
            v->priority = prio;
            v->core = uint8_t(core);
            v->port = uint8_t(n.port);
            v->group = n.pl ? uint8_t(n.pl->index) : uint8_t(2);
            v->pl = n.pl;
            v->ch = n.ch;
            v->note_id = n.note_id;
            v->handle = n.handle;
            v->se_id = n.se_id;
            v->pcm = &pcm;
            v->n_src = int32_t(pcm.data.size());
            v->looping = pcm.looping;
            v->ls = pcm.looping ? pcm.loop_start : 0;
            v->send_l = (sf & 4) != 0;
            v->send_r = (sf & 8) != 0;
            const double note_f = n.key + po.transpose + sp.transpose + (po.detune + sm.fine + sp.detune) / 128.0;
            v->base_ratio = (double(L_.vag->rate) / double(kRate)) * std::exp2((note_f - sm.base_key) / 12.0);
            const int curve = L_.sset->vel_curve & 15;
            const int v_eff = (curve != 2) ? n.vel : std::max(1, n.vel * n.vel / 127);
            v->amp = float((po.vol / 128.0) * (sp.vol / 128.0) * (sm.volume / 128.0) * (v_eff / 127.0));
            v->gain = n.gain;
            double pan0 = (po.pan - 64) + (sp.pan - 64) + (sm.pan - 64) + 64;
            if (po.pan_key_amount) pan0 += (n.key - po.pan_key_center) * po.pan_key_amount / 12.0;
            if (sp.pan_key_amount) pan0 += (n.key - sp.pan_key_center) * sp.pan_key_amount / 12.0;
            v->pan_base = float(pan0);
            v->pan_off = n.pan_off;
            v->bend_hi = sp.bend_hi / 256.f;
            v->bend_lo = sp.bend_lo / 256.f;
            v->natural = uint32_t(double(v->n_src) / std::max(v->base_ratio, 1e-6));
            v->env.start(sm.adsr1, sm.adsr2);
        }
    }

    void key_off_voice(Voice& v) {
        if (!v.released) { v.released = true; v.env.key_off(); }
    }
    void keyoff_note(uint32_t id) {
        for (Voice& v : voices)
            if (v.active && v.note_id == id && v.handle == 0) key_off_voice(v);
    }
    void keyoff_player(Player& p) {
        for (Voice& v : voices)
            if (v.active && v.pl == &p) key_off_voice(v);
    }
    void kill_port(int port) {
        for (Voice& v : voices)
            if (v.active && v.port == port) v.active = false;
    }

    // ---- sequencer ------------------------------------------------------------------------------------------------------------------------------
    void schedule(Player& p) {
        if (!p.song || p.ev >= p.song->events.size()) { p.playing = false; return; }
        const Ev& e = p.song->events[p.ev];
        p.next_sec = p.sec + double(int64_t(e.tick) - int64_t(p.last_tick)) * p.tempo / 1e6 / double(p.song->division);
        p.next_frame = p.start_frame + int64_t(std::llround(p.next_sec * kRate));
    }

    void player_stop(Player& p) {
        keyoff_player(p);
        p.playing = false;
        p.paused = false;
        p.fade = Fade();
        for (Chan& c : p.ch) { std::memset(c.nfifo, 0, sizeof c.nfifo); c.npending = 0; }
    }

    void player_play(Player& p, int index) {
        player_stop(p);
        if (!p.songs || index < 0 || size_t(index) >= p.songs->songs.size()) return;
        const SongEntry& e = p.songs->songs[size_t(index)];
        p.song = e.song.get();
        p.vol = p.default_vol = e.volume;
        p.gain_cur = float(p.vol) / 256.f;
        for (Chan& c : p.ch) c.reset(p.port);
        p.ev = 0; p.last_tick = 0; p.sec = 0; p.tempo = 500000.0;
        p.start_frame = frame;
        p.nrpn = -1; p.dentry = -1;
        for (auto& l : p.loops) l = Player::Loop();
        std::memset(p.endless_done, 0, sizeof p.endless_done);
        p.playing = true;
        schedule(p);
    }

    void fire(Player& p) {
        const Ev& e = p.song->events[p.ev];
        p.sec = p.next_sec;
        p.last_tick = e.tick;
        size_t next = p.ev + 1;
        Chan& c = p.ch[e.ch & 15];
        switch (e.kind) {
            case EvTempo: p.tempo = double(e.val); break;
            case EvEnd:
                keyoff_player(p);
                p.playing = false;
                return;
            case EvPc: c.prog = e.a; break;
            case EvBend: c.bend = uint16_t((e.b << 7) | e.a); break;
            case EvOn: {
                const uint32_t id = ++note_counter;
                if (c.nfifo[e.a] >= 4) {
                    std::memmove(&c.fifo[e.a][0], &c.fifo[e.a][1], 3 * sizeof(uint32_t));
                    c.nfifo[e.a] = 3;
                }
                c.fifo[e.a][c.nfifo[e.a]++] = id;
                NoteReq n;
                n.pl = &p; n.ch = &c; n.port = c.port; n.prog = c.prog; n.key = e.a; n.vel = e.b; n.note_id = id;
                start_note(n);
                break;
            }
            case EvOff: {
                if (c.nfifo[e.a] == 0) break;
                const uint32_t id = c.fifo[e.a][0];
                std::memmove(&c.fifo[e.a][0], &c.fifo[e.a][1], 3 * sizeof(uint32_t));
                --c.nfifo[e.a];
                if (c.pedal && c.npending < 32) c.pending[c.npending++] = id;
                else keyoff_note(id);
                break;
            }
            case EvCc: {
                switch (e.a) {
                    case 7: c.vol = e.b; break;
                    case 10: c.pan = e.b; break;
                    case 11: c.expr = e.b; break;
                    case 32: c.port = e.b & 15; break;
                    case 64: {
                        const bool down = e.b >= 64;
                        if (c.pedal && !down) {
                            for (int i = 0; i < c.npending; ++i) keyoff_note(c.pending[i]);
                            c.npending = 0;
                        }
                        c.pedal = down;
                        break;
                    }
                    case 99: p.nrpn = e.b; p.dentry = -1; break;
                    case 6:
                        p.dentry = e.b;
                        if (p.nrpn == 0) { auto& l = p.loops[e.b & 127]; l.valid = true; l.ev = p.ev; l.tick = e.tick; l.remaining = -1; }
                        break;
                    case 38:
                        if (p.nrpn == 1 && p.dentry >= 0) {
                            auto& l = p.loops[p.dentry & 127];
                            const int cnt = e.b;
                            if (l.valid) {
                                if (l.remaining == -1) l.remaining = cnt;
                                if (cnt == 0 || l.remaining != 0) {
                                    bool jump = true;
                                    if (cnt == 0) {
                                        int& done = p.endless_done[p.dentry & 127];
                                        if (done < 0x3FFFFFFF) ++done;
                                        if (loop_limit >= 0 && done > loop_limit) { p.nrpn = -1; jump = false; }
                                    } else {
                                        --l.remaining;
                                    }
                                    if (jump) { next = l.ev + 1; p.last_tick = l.tick; }
                                } else {
                                    l.remaining = -1;
                                }
                            }
                        }
                        break;
                    default: break;
                }
                break;
            }
            default: break;
        }
        p.ev = next;
        schedule(p);
    }

    // ---- fades / 60 Hz step ---------------------------------------------------------------------------------------------------------------------
    void set_player_vol_checked(Player& p, int v) {   // SndSetBgmVol
        if (p.playing && v >= 0 && v < 0x80 && v != p.vol) p.vol = v;
    }

    void fade_step(Player& p) {
        Fade& f = p.fade;
        if (f.mode == 0) return;
        f.now += f.step;
        int mode = f.mode;
        if (std::fabs(f.step) < 1e-4f) mode = 0;
        bool finished = false;
        if (mode < 1) {
            if (f.now <= f.target) { finished = true; f.now = f.target; }
        } else if (f.target <= f.now) {
            finished = true;
            f.now = f.target;
        }
        if (mode == 0) finished = true;
        if (f.stop) {
            p.vol = std::max(0, int(f.now));
            if (finished) { player_stop(p); return; }
        } else {
            set_player_vol_checked(p, int(f.now));
        }
        if (finished) f.mode = 0;
    }

    void on_tick() {
        for (Player& p : players) fade_step(p);
        for (Sched& s : sched) {
            if (!s.handle) continue;
            if (--s.left <= 0) {
                for (Voice& v : voices)
                    if (v.active && v.handle == s.handle) key_off_voice(v);
                s.handle = 0;
            }
        }
    }

    // ---- commands -------------------------------------------------------------------------------------------------------------------------------
    void apply(const Cmd& c) {
        switch (c.type) {
            case CSetBank:
                kill_port(c.i[0]);
                banks[size_t(c.i[0] & 15)] = c.bank;
                break;
            case CSetSongs: {
                Player& p = players[c.i[0] & 1];
                player_stop(p);
                p.songs = c.songs;
                p.song = nullptr;
                break;
            }
            case CPlay: player_play(players[c.i[0] & 1], c.i[1]); break;
            case CStop: {
                Player& p = players[c.i[0] & 1];
                if (c.i[1] > 0 && p.playing) {
                    p.fade.mode = -1; p.fade.now = float(p.vol); p.fade.target = 0; p.fade.step = -float(p.vol) / float(c.i[1]); p.fade.stop = true;
                } else {
                    player_stop(p);
                }
                break;
            }
            case CPause: {
                Player& p = players[c.i[0] & 1];
                if (p.playing && !p.paused) { p.paused = true; p.pause_frame = frame; keyoff_player(p); }
                break;
            }
            case CResume: {
                Player& p = players[c.i[0] & 1];
                if (p.playing && p.paused) {
                    p.paused = false;
                    const int64_t d = frame - p.pause_frame;
                    p.start_frame += d;
                    p.next_frame += d;
                }
                break;
            }
            case CSetVol: {   // i1: 0 = unchecked int, 1 = checked int (SndSetBgmVol), 2 = checked fraction, 3 = unchecked fraction
                Player& p = players[c.i[0] & 1];
                const int mode = c.i[1];
                const int v = (mode >= 2) ? int(c.f[0] * float(p.default_vol)) : c.i[2];
                if (mode == 1 || mode == 2) set_player_vol_checked(p, v);
                else p.vol = std::clamp(v, 0, 255);
                break;
            }
            case CFade: {   // i1 = +1 fade in / -1 fade out, i2 = frames, i3 = target, i4 = from (fade in)
                Player& p = players[c.i[0] & 1];
                const int frames = c.i[2];
                if (frames <= 0) break;
                Fade& f = p.fade;
                f.stop = false;
                if (c.i[1] > 0) {
                    const int target = c.i[3] < 0 ? p.default_vol : c.i[3];
                    const int from = c.i[4] < 0 ? p.vol : c.i[4];
                    if (from == p.default_vol) break;
                    f.now = p.default_vol ? float(from) / float(p.default_vol) : 0.f;   // quirk of SndBgmFadeIn
                    f.target = float(target);
                    f.step = (f.target - f.now) / float(frames);
                    f.mode = 1;
                } else {
                    f.now = float(p.vol);
                    f.target = float(c.i[3]);
                    f.step = (f.target - f.now) / float(frames);
                    f.mode = -1;
                }
                break;
            }
            case CLoopLimit: loop_limit = c.i[0]; break;
            case CPlayNote: {
                NoteReq n;
                n.port = c.i[0]; n.prog = c.i[1]; n.key = c.i[2]; n.vel = c.i[3]; n.se_id = c.i[4];
                n.pan_off = c.f[1]; n.gain = c.f[0]; n.handle = c.u;
                start_note(n);
                if (c.i[5] > 0) {
                    for (Sched& s : sched)
                        if (!s.handle) { s.handle = c.u; s.left = c.i[5]; break; }
                }
                break;
            }
            case CStopSe:
                for (Voice& v : voices)
                    if (v.active && v.handle != 0 && v.se_id == c.i[0]) key_off_voice(v);
                break;
            case CStopHandle:
                for (Voice& v : voices)
                    if (v.active && v.handle == c.u) key_off_voice(v);
                break;
            case CStopAllSe:
                for (Voice& v : voices)
                    if (v.active && v.handle != 0) key_off_voice(v);
                break;
            case CSeVol:
                for (Voice& v : voices)
                    if (v.active && v.handle != 0 && v.se_id == c.i[0]) v.gain = c.f[0];
                break;
            case CSePan:
                for (Voice& v : voices)
                    if (v.active && v.handle != 0 && v.se_id == c.i[0]) v.pan_off = c.f[0];
                break;
            case CReverb: rv[c.i[0] & 1].configure(c.i[1], c.i[2]); break;
            case CMaster: master = c.f[0]; break;
            case CStereo: stereo = c.i[0] != 0; break;
            case CReverbOn: reverb_on = c.i[0] != 0; break;
            case CSendAll: send_all = c.i[0] != 0; break;
            case CDiffusion: rv[0].diffusion = rv[1].diffusion = c.i[0] != 0; break;
            case CPresets:
                for (Reverb& r : rv) { r.presets = c.i[0] != 0; r.configure(r.mode, r.depth); }
                break;
            case CLimiter: limiter = c.i[0] != 0; break;
            case CVoiceLimit: voice_limit[0] = std::max(1, c.i[0]); voice_limit[1] = std::max(1, c.i[1]); break;
            case CStopPort:
                for (Voice& v : voices)
                    if (v.active && v.handle != 0 && v.port == c.i[0]) key_off_voice(v);
                break;
        }
    }

    // ---- mixing ---------------------------------------------------------------------------------------------------------------------------------
    void mix_voice(Voice& v, int n, const float g0[3], const float g1[3]) {
        double ratio = v.base_ratio;
        float chvol = 1.f, pan = v.pan_base + v.pan_off;
        if (v.ch) {
            const Chan& c = *v.ch;
            chvol = float(double(c.vol) / 127.0 * double(c.expr) / 127.0);
            pan += float(c.pan) - 64.f;
            const double bend = double(int(c.bend) - 0x2000);
            const double semis = bend >= 0 ? bend / 8192.0 * v.bend_hi : bend / 8192.0 * v.bend_lo;
            if (semis != 0.0) ratio *= std::exp2(semis / 12.0);
        }
        ratio = std::min(ratio, double(0x3FFF) / double(0x1000));
        const float d = pan - 64.f;
        const float gl = d > 0.f ? std::clamp((63.f - d) / 63.f, 0.f, 1.f) : 1.f;
        const float gr = d > 0.f ? 1.f : std::clamp((63.f + d) / 63.f, 0.f, 1.f);
        const float amp = v.amp * v.gain * chvol;
        const float ga = g0[v.group], gb = g1[v.group];
        const float gstep = (gb - ga) / float(n);
        float g = ga;
        const bool to_rv = reverb_on && rv[v.core].depth > 0;
        const bool sl_ = to_rv && (send_all || v.send_l), sr_ = to_rv && (send_all || v.send_r);
        float* rl = RIN[v.core][0];
        float* rr = RIN[v.core][1];
        const int16_t* data = v.pcm->data.data();
        const int32_t nsrc = v.n_src;
        const double loopn = double(std::max(nsrc - v.ls, 1));
        for (int i = 0; i < n; ++i) {
            if (!v.looping && (v.pos >= double(nsrc - 1) || v.k >= v.natural)) { v.active = false; return; }
            const float lvl = v.env.next();
            if (lvl < 0.f) { v.active = false; return; }
            if (v.released && ++v.rel_n > uint32_t(10 * kRate)) { v.active = false; return; }
            int i0 = int(v.pos);
            const float fr = float(v.pos - double(i0));
            if (i0 >= nsrc) i0 = nsrc - 1;
            int i1 = i0 + 1;
            if (i1 >= nsrc) i1 = v.looping ? v.ls : nsrc - 1;
            const float s = (float(data[i0]) * (1.f - fr) + float(data[i1]) * fr) * (1.f / 32768.f);
            const float o = s * lvl * amp * g;
            L[i] += o * gl;
            R[i] += o * gr;
            if (sl_) rl[i] += o * gl;
            if (sr_) rr[i] += o * gr;
            g += gstep;
            v.pos += ratio;
            ++v.k;
            if (v.looping && v.pos >= double(nsrc)) v.pos = double(v.ls) + std::fmod(v.pos - double(v.ls), loopn);
        }
    }

    void mix_segment(float* out, int n) {
        std::memset(L, 0, sizeof(float) * size_t(n));
        std::memset(R, 0, sizeof(float) * size_t(n));
        std::memset(RIN, 0, sizeof RIN);
        // group gains (0 = BGM player, 1 = ambient player, 2 = everything else): slew to the port volume
        float g0[3], g1[3];
        for (int i = 0; i < 2; ++i) {
            Player& p = players[i];
            const float target = float(p.vol) / 256.f;
            g0[i] = p.gain_cur;
            const float maxd = float(n) / 1024.f;
            p.gain_cur += std::clamp(target - p.gain_cur, -maxd, maxd);
            g1[i] = p.gain_cur;
        }
        g0[2] = g1[2] = 1.f;
        for (Voice& v : voices)
            if (v.active) mix_voice(v, n, g0, g1);
        if (reverb_on)
            for (int c = 0; c < 2; ++c)
                if (rv[c].depth > 0) rv[c].process(RIN[c][0], RIN[c][1], L, R, n);
        for (int i = 0; i < n; ++i) {
            float l = L[i] * master, r = R[i] * master;
            if (!stereo) l = r = 0.5f * (l + r);
            if (limiter) {
                auto soft = [](float x) {
                    const float a = std::fabs(x);
                    if (a <= 0.9f) return x;
                    return std::copysign(0.9f + 0.1f * std::tanh((a - 0.9f) * 10.f), x);
                };
                l = soft(l);
                r = soft(r);
            }
            out[2 * i] = l;
            out[2 * i + 1] = r;
        }
    }

    void render(float* out, int frames) {
        int done = 0;
        while (done < frames) {
            for (Player& p : players) {
                int guard = 0;
                while (p.playing && !p.paused && p.next_frame <= frame && ++guard < 100000) fire(p);
            }
            int n = std::min({frames - done, kBlock, tick_acc});
            for (Player& p : players)
                if (p.playing && !p.paused) {
                    const int64_t until = p.next_frame - frame;
                    if (until > 0 && until < n) n = int(until);
                }
            if (n < 1) n = 1;
            mix_segment(out + 2 * size_t(done), n);
            frame += n;
            done += n;
            tick_acc -= n;
            if (tick_acc <= 0) { tick_acc = kFrameSamples; on_tick(); }
        }
        int a = 0;
        for (const Voice& v : voices) a += v.active ? 1 : 0;
        active = a;
        peak = std::max(peak, a);
    }
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// SoundSystem: game-thread side
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct SoundSystem::Impl {
    SoundSystem& sys;
    const Disc& disc;
    SndTables tables;
    Engine eng;
    std::mutex mu;
    std::vector<Cmd> q, local;
    std::array<std::shared_ptr<Bank>, kPorts> banks;
    int basic = -1, cap = -1, n_bgm = 0, n_amb = 0;
    std::atomic<uint32_t> next_handle{1};
    Vec3 cam_pos, cam_dir{0, 0, 1};

    Impl(SoundSystem& s, const Disc& d) : sys(s), disc(d) {
        q.reserve(256);
        local.reserve(256);
    }

    void post(Cmd&& c) {
        std::lock_guard<std::mutex> lk(mu);
        q.push_back(std::move(c));
    }
    void post_i(CmdType t, int a = 0, int b = 0, int c = 0) {
        Cmd cmd;
        cmd.type = t; cmd.i[0] = a; cmd.i[1] = b; cmd.i[2] = c;
        post(std::move(cmd));
    }
    void set_bank(int port, std::shared_ptr<Bank> b) {
        banks[size_t(port)] = b;
        Cmd c;
        c.type = CSetBank; c.i[0] = port; c.bank = std::move(b);
        post(std::move(c));
    }
    void set_reverb(int core, std::optional<std::pair<int, int>> r) {
        Cmd c;
        c.type = CReverb; c.i[0] = core; c.i[1] = r ? r->first : 0; c.i[2] = r ? r->second : 0;
        post(std::move(c));
    }
    SndPack open(const char* sub, const char* stem_fmt, int n) {
        char stem[64], path[96];
        std::snprintf(stem, sizeof stem, stem_fmt, n);
        std::snprintf(path, sizeof path, "sound/%s/%s.snd", sub, stem);
        return SndPack(disc.read(path), stem);
    }

    void render(float* out, int frames) {
        {
            std::unique_lock<std::mutex> lk(mu, std::try_to_lock);
            if (lk.owns_lock()) local.swap(q);
        }
        for (const Cmd& c : local) eng.apply(c);
        local.clear();
        eng.render(out, frames);
        sys.active_.store(eng.active);
        sys.peak_.store(eng.peak);
        sys.missing_.store(eng.missing);
        sys.bgm_playing_.store(eng.players[0].playing);
        sys.amb_playing_.store(eng.players[1].playing);
        sys.bgm_vol_.store(eng.players[0].vol);
        sys.bgm_fading_.store(eng.players[0].fade.mode != 0);
        sys.bgm_pos_.store(eng.players[0].sec);
    }

    SeHandle play_note(int port, int prog, int key, int se_id, int vol, int pan_off, int stop_frames = 0) {
        if (vol <= 0) return 0;
        Cmd c;
        c.type = CPlayNote;
        c.i[0] = port; c.i[1] = prog; c.i[2] = key; c.i[3] = 127; c.i[4] = se_id; c.i[5] = stop_frames;
        c.f[0] = float(vol) / 127.f;
        c.f[1] = float(pan_off);
        c.u = next_handle.fetch_add(1);
        const uint32_t h = c.u;
        post(std::move(c));
        return h;
    }

    int scaled_volume(int base, float volf) const {
        if (volf < 0.f) return base;
        return std::clamp(int(float(base) * volf), 0, 127);
    }
};

SoundSystem::SoundSystem(const Disc& disc) {
    impl_ = std::make_unique<Impl>(*this, disc);
    impl_->tables = SndTables::load(Elf(disc.read_root("SCUS_971.11")), disc);
}

SoundSystem::~SoundSystem() = default;

void SoundSystem::render(float* out, int frames) { impl_->render(out, frames); }

// ---- music ----------------------------------------------------------------------------------------------------------------------------------------
void SoundSystem::load_bgm(int n) {
    SndPack pk = impl_->open("bgm", "bgm%d", n);
    const auto banks = pk.banks();
    if (banks.empty()) throw Error("bgm pack without bank");
    std::shared_ptr<Bank> a;
    for (const auto& b : banks)
        if (b.first == 'a') a = b.second;
    if (!a) a = banks.front().second;
    auto all = pk.songs(&impl_->tables.sq_vol);
    auto set = std::make_shared<SongSet>();
    for (auto& s : all)
        if (s.letter == 'a') set->songs.push_back(s);
    if (set->songs.empty()) set->songs = all;
    Cmd c;
    c.type = CSetSongs; c.i[0] = 0; c.songs = set;
    impl_->post(std::move(c));
    impl_->set_bank(0, a);
    impl_->set_reverb(0, pk.reverb);
    bgm_no_ = n;
    impl_->n_bgm = int(set->songs.size());
    bgm_default_vol_ = set->songs.empty() ? 0 : set->songs[0].volume;
}

int SoundSystem::bgm_sequence_count() const { return impl_->n_bgm; }
int SoundSystem::ambient_sequence_count() const { return impl_->n_amb; }

void SoundSystem::play_bgm(int index) { impl_->post_i(CPlay, 0, index); }
void SoundSystem::stop_bgm(int fade_frames) { impl_->post_i(CStop, 0, fade_frames); }
void SoundSystem::pause_bgm() { impl_->post_i(CPause, 0); }
void SoundSystem::resume_bgm() { impl_->post_i(CResume, 0); }
void SoundSystem::set_bgm_volume(int v) { impl_->post_i(CSetVol, 0, 1, v); }
void SoundSystem::set_bgm_volume_f(float f) {
    Cmd c;
    c.type = CSetVol; c.i[0] = 0; c.i[1] = 2; c.f[0] = f;
    impl_->post(std::move(c));
}
void SoundSystem::bgm_fade_in(int frames, int target, int from) {
    Cmd c;
    c.type = CFade; c.i[0] = 0; c.i[1] = 1; c.i[2] = frames; c.i[3] = target; c.i[4] = from;
    impl_->post(std::move(c));
}
void SoundSystem::bgm_fade_out(int frames, int target) {
    Cmd c;
    c.type = CFade; c.i[0] = 0; c.i[1] = -1; c.i[2] = frames; c.i[3] = target;
    impl_->post(std::move(c));
}

void SoundSystem::set_bgm_loop_limit(int n) { impl_->post_i(CLoopLimit, n); }

// ---- sound sets -----------------------------------------------------------------------------------------------------------------------------------
void SoundSystem::load_music_pack(Bytes data, const std::string& stem) {
    SndPack pk(std::move(data), stem);
    const auto banks = pk.banks();
    auto find = [&](char l) -> std::shared_ptr<Bank> {
        std::shared_ptr<Bank> r;
        for (const auto& b : banks)
            if (b.first == l) r = b.second;
        return r;
    };
    auto all = pk.songs(&impl_->tables.sq_vol);
    auto aset = std::make_shared<SongSet>();
    auto eset = std::make_shared<SongSet>();
    for (auto& s : all) {
        if (s.letter == 'a') aset->songs.push_back(s);
        else if (s.letter == 'e') eset->songs.push_back(s);
    }
    impl_->post_i(CStopAllSe);
    Cmd c0;
    c0.type = CSetSongs; c0.i[0] = 0; c0.songs = aset;
    impl_->post(std::move(c0));
    Cmd c1;
    c1.type = CSetSongs; c1.i[0] = 1; c1.songs = eset;
    impl_->post(std::move(c1));
    impl_->n_bgm = int(aset->songs.size());
    impl_->n_amb = int(eset->songs.size());
    bgm_default_vol_ = aset->songs.empty() ? 0 : aset->songs[0].volume;
    impl_->set_bank(0, find('a'));
    impl_->set_bank(15, find('c'));
    impl_->set_bank(1, find('e'));
    impl_->set_bank(2, find('i'));
    impl_->set_bank(14, find('i'));
    impl_->set_bank(10, find('g'));
    impl_->set_bank(13, find('m'));
    impl_->set_reverb(0, pk.reverb);
    impl_->set_reverb(1, pk.reverb);
    bgm_no_ = -1;
}

SeHandle SoundSystem::play_pk(int port, int prog, int key, float volf) {
    return impl_->play_note(port, prog, key, 20000 + prog * 128 + key, impl_->scaled_volume(impl_->tables.volume_of(prog, key), volf), 0);
}

void SoundSystem::stop_pk(int prog, int key) { impl_->post_i(CStopSe, 20000 + prog * 128 + key); }

void SoundSystem::load_sound_set(int n) {
    SndPack pk = impl_->open("set", "snd%d", n);
    const auto banks = pk.banks();
    auto find = [&](char l) -> std::shared_ptr<Bank> {
        std::shared_ptr<Bank> r;
        for (const auto& b : banks)
            if (b.first == l) r = b.second;
        return r;
    };
    impl_->post_i(CStopAllSe);
    auto all = pk.songs(&impl_->tables.sq_vol);
    auto eset = std::make_shared<SongSet>();
    for (auto& s : all)
        if (s.letter == 'e') eset->songs.push_back(s);
    Cmd c;
    c.type = CSetSongs; c.i[0] = 1; c.songs = eset;
    impl_->post(std::move(c));
    impl_->n_amb = int(eset->songs.size());
    impl_->set_bank(15, find('c'));
    impl_->set_bank(1, find('e'));
    impl_->set_bank(2, find('i'));
    impl_->set_bank(14, find('i'));
    impl_->set_bank(10, find('g'));
    impl_->set_bank(13, find('m'));
    impl_->basic = pk.table ? pk.table->first : -1;
    impl_->cap = pk.table ? pk.table->second : -1;
    impl_->set_reverb(1, pk.reverb);
    sound_set_ = n;
}

void SoundSystem::play_ambient(int index) { impl_->post_i(CPlay, 1, index); }
void SoundSystem::stop_ambient() { impl_->post_i(CStop, 1, 0); }
void SoundSystem::set_ambient_volume(int v) { impl_->post_i(CSetVol, 1, 0, v); }
void SoundSystem::set_ambient_volume_f(float f) {
    Cmd c;
    c.type = CSetVol; c.i[0] = 1; c.i[1] = 3; c.f[0] = f;
    impl_->post(std::move(c));
}

// ---- sound effects --------------------------------------------------------------------------------------------------------------------------------
std::optional<SoundSystem::SeInfo> SoundSystem::se_info(int id) const {
    auto e = impl_->tables.lookup(id, impl_->basic, impl_->cap, voice_set_);
    if (!e) return std::nullopt;
    return SeInfo{e->prog, e->key, e->port >= 0 ? int(e->port) : 14, impl_->tables.volume_of(e->prog, e->key)};
}

int SoundSystem::se_volume(int id, float volf) const {
    auto i = se_info(id);
    if (!i) return 0;
    return impl_->scaled_volume(i->volume, volf);
}

bool SoundSystem::se_is_looping(int id) const {
    auto i = se_info(id);
    if (!i) return false;
    const Bank* b = impl_->banks[size_t(i->port & 15)].get();
    if (!b || b->empty) return false;
    Bank::Layer lay[kMaxLayers];
    const int n = b->resolve(i->prog, i->key, 127, lay, kMaxLayers);
    for (int k = 0; k < n; ++k)
        if (lay[k].pcm->looping) return true;
    return false;
}

static int pan_offset(float panf) {   // SndGetPanf - 0x40
    const float p = std::clamp(panf, -1.f, 1.f);
    return int(p * 63.f);
}

SeHandle SoundSystem::play_se(int id, float volf, float panf) {
    auto i = se_info(id);
    if (!i) return 0;
    return impl_->play_note(i->port, i->prog, i->key, id, impl_->scaled_volume(i->volume, volf), pan_offset(panf));
}

SeHandle SoundSystem::play_se_timed(int id, int frames, float volf, float panf) {
    auto i = se_info(id);
    if (!i) return 0;
    return impl_->play_note(i->port, i->prog, i->key, id, impl_->scaled_volume(i->volume, volf), pan_offset(panf), std::max(frames, 1));
}

static Vec3 flat_normalize(Vec3 v) {
    v.y = 0;
    const float l = std::sqrt(v.x * v.x + v.z * v.z);
    if (l < 1e-12f) return Vec3{0, 0, 0};
    return Vec3{v.x / l, 0, v.z / l};
}

SeHandle SoundSystem::play_se_at(int id, Vec3 pos, float near_d, float far_d) {
    if (near_d < 0.f) near_d = 20.f;
    if (far_d < 0.f) far_d = 500.f;
    const Vec3 cp = impl_->cam_pos;
    const float dx = pos.x - cp.x, dy = pos.y - cp.y, dz = pos.z - cp.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float volf = 1.f - (dist - near_d) / (far_d - near_d);
    if (far_d < dist) volf = 0.f;
    if (dist < near_d) volf = 1.f;
    const Vec3 cd = flat_normalize(impl_->cam_dir);
    const Vec3 right{cd.z, 0.f, -cd.x};
    const Vec3 to = flat_normalize(Vec3{dx, 0.f, dz});
    float dot = -(to.x * right.x + to.z * right.z);
    float sign = 1.f;
    if (dot < 0.f) { sign = -1.f; dot = -dot; }
    const float pan = 0.7f * sign * dot * dot * dot * dot;
    volf = volf * (0.7f + 0.3f * std::fabs(pan)) * 1.4f;
    return play_se(id, volf, pan);
}

void SoundSystem::stop_se(int id) { impl_->post_i(CStopSe, id); }
void SoundSystem::stop_se_handle(SeHandle h) {
    Cmd c;
    c.type = CStopHandle; c.u = h;
    impl_->post(std::move(c));
}
void SoundSystem::stop_all_se() { impl_->post_i(CStopAllSe); }

void SoundSystem::set_se_volume(int id, float volf) {
    auto i = se_info(id);
    if (!i) return;
    Cmd c;
    c.type = CSeVol; c.i[0] = id; c.f[0] = float(impl_->scaled_volume(i->volume, volf)) / 127.f;
    impl_->post(std::move(c));
}
void SoundSystem::set_se_pan(int id, float panf) {
    Cmd c;
    c.type = CSePan; c.i[0] = id; c.f[0] = float(pan_offset(panf));
    impl_->post(std::move(c));
}

void SoundSystem::set_listener(Vec3 pos, Vec3 dir) {
    impl_->cam_pos = pos;
    impl_->cam_dir = dir;
}

// ---- voices / special -----------------------------------------------------------------------------------------------------------------------------
void SoundSystem::load_voice(int character) {
    SndPack pk = impl_->open("voice", "voice%d", character);
    const auto banks = pk.banks();
    if (banks.empty()) throw Error("voice pack without bank");
    impl_->set_bank(11, banks.front().second);
    voice_set_ = character;
}

void SoundSystem::load_special(int n) {
    SndPack pk = impl_->open("special", "sp%d", n);
    const auto banks = pk.banks();
    impl_->set_bank(12, banks.empty() ? nullptr : banks.front().second);
}

SeHandle SoundSystem::play_special(int index, float volf, float panf) {
    if (index < 0 || size_t(index) >= impl_->tables.special.size()) return 0;
    const SeEntry& e = impl_->tables.special[size_t(index)];
    if (e.prog < 0) return 0;
    return impl_->play_note(12, e.prog, e.key, 10000 + index, impl_->scaled_volume(impl_->tables.volume_of(e.prog, e.key), volf), pan_offset(panf));
}

void SoundSystem::stop_special() { impl_->post_i(CStopPort, 12); }

// ---- mixer settings -------------------------------------------------------------------------------------------------------------------------------
void SoundSystem::set_master_volume(float v) {
    Cmd c;
    c.type = CMaster; c.f[0] = v;
    impl_->post(std::move(c));
}
void SoundSystem::set_stereo(bool s) { impl_->post_i(CStereo, s); }
void SoundSystem::set_reverb_enabled(bool on) { impl_->post_i(CReverbOn, on); }
void SoundSystem::set_reverb_send_all(bool on) { impl_->post_i(CSendAll, on); }
void SoundSystem::set_reverb_diffusion(bool on) { impl_->post_i(CDiffusion, on); }
void SoundSystem::set_reverb_presets(bool on) { impl_->post_i(CPresets, on); }
void SoundSystem::set_limiter(bool on) { impl_->post_i(CLimiter, on); }
void SoundSystem::set_voice_limit(int core0, int core1) { impl_->post_i(CVoiceLimit, core0, core1); }

}  // namespace dccore::snd
