// dcsound: offline renderer / checker for the C++ sound engine (dccore/sound.h). Output formats match tools/dc/snd.py (48 kHz stereo 16-bit WAV).
//
//   dcsound <iso> bgm <n> <seconds> <out.wav> [--seq i] [--loops N] [--noreverb]      seconds 0 = play the sequence once through (with --loops N extra loops)
//   dcsound <iso> amb <set> <idx> <seconds> <out.wav> [--loops N] [--noreverb]        ambient (type E) sequence #idx of a sound set
//   dcsound <iso> se <set> <id> <out.wav> [--hold s] [--voice c] [--special n] [--reverb]   one sound effect (looping SEs are stopped after --hold, default 1 s)
//   dcsound <iso> special <n> <out.wav>                                               special SE n (special/sp<n>.snd)
//   dcsound compare <ref.wav> <test.wav> [seconds]                                    RMS difference / correlation of two WAV files
// Options: --game (engine defaults: 24 voices/core, per-sample reverb sends, allpass diffusion, soft limiter) instead of the snd.py-equivalent reference mode;
//          --chunk N (render in chunks of N frames, default 1024), --raw (no snd.py style peak normalisation), --voices N, --flags, --diffusion, --limiter.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <fstream>
#include <random>
#include <thread>
#include <string>
#include <vector>
#include "dccore/sound.h"

using namespace dccore;
using namespace dccore::snd;

namespace {

struct Wav { int rate = 0, channels = 0; std::vector<float> data; };   // interleaved

void write_wav(const std::string& path, const std::vector<float>& st, int rate) {
    std::vector<int16_t> pcm(st.size());
    for (size_t i = 0; i < st.size(); ++i) {
        const double v = std::nearbyint(double(st[i]) * 32767.0);
        pcm[i] = int16_t(std::max(-32768.0, std::min(32767.0, v)));
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) throw Error("cannot write " + path);
    const uint32_t data_bytes = uint32_t(pcm.size() * 2);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); w32(36 + data_bytes); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(2); w32(uint32_t(rate)); w32(uint32_t(rate) * 4); w16(4); w16(16);
    f.write("data", 4); w32(data_bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), std::streamsize(data_bytes));
}

Wav read_wav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("cannot read " + path);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0) throw Error("not a WAV: " + path);
    Wav w;
    size_t p = 12;
    int bits = 0;
    while (p + 8 <= b.size()) {
        const uint32_t sz = rd<uint32_t>(b, p + 4);
        if (std::memcmp(b.data() + p, "fmt ", 4) == 0) {
            w.channels = rd<uint16_t>(b, p + 10);
            w.rate = int(rd<uint32_t>(b, p + 12));
            bits = rd<uint16_t>(b, p + 22);
        } else if (std::memcmp(b.data() + p, "data", 4) == 0) {
            if (bits != 16) throw Error("only 16-bit WAV supported");
            const size_t n = std::min<size_t>(sz, b.size() - p - 8) / 2;
            w.data.resize(n);
            for (size_t i = 0; i < n; ++i) w.data[i] = float(rd<int16_t>(b, p + 8 + i * 2)) / 32768.f;
            return w;
        }
        p += 8 + sz + (sz & 1);
    }
    throw Error("no data chunk in " + path);
}

struct Opts {
    bool game = false, raw = false, noreverb = false, flags = false, diffusion = false, limiter = false, reverb = false;
    int voices = 256, chunk = 1024, seq = 0, loops = 1, voice = -1, special = -1;
    double hold = -1;
};

void configure(SoundSystem& s, const Opts& o) {
    if (!o.game) {
        s.set_reverb_send_all(!o.flags);
        s.set_reverb_diffusion(o.diffusion);
        s.set_reverb_presets(false);
        s.set_limiter(o.limiter);
        s.set_voice_limit(o.voices, o.voices);
    } else if (o.voices != 256) {
        s.set_voice_limit(o.voices, o.voices);
    }
}

// Renders `frames` frames (or until `idle()` after at least min_frames) and returns the interleaved stereo buffer.
template <class Idle>
std::vector<float> render_until(SoundSystem& s, size_t min_frames, size_t max_frames, int chunk, Idle idle, double* rt = nullptr) {
    std::vector<float> out;
    std::vector<float> buf(size_t(chunk) * 2);
    const auto t0 = std::chrono::steady_clock::now();
    size_t done = 0;
    while (done < max_frames) {
        const int n = int(std::min<size_t>(size_t(chunk), max_frames - done));
        s.render(buf.data(), n);
        out.insert(out.end(), buf.begin(), buf.begin() + n * 2);
        done += size_t(n);
        if (done >= min_frames && idle()) break;
    }
    if (rt) {
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        *rt = secs > 0 ? (double(done) / kRate) / secs : 0;
    }
    return out;
}

void trim_silence(std::vector<float>& st) {   // trailing digital silence (chunk granularity of the idle test)
    size_t n = st.size();
    while (n >= 2 && st[n - 1] == 0.f && st[n - 2] == 0.f) n -= 2;
    st.resize(n);
}

void finish(std::vector<float>& st, const std::string& path, const Opts& o, SoundSystem& s, double rt) {
    float peak = 0;
    for (float v : st) peak = std::max(peak, std::fabs(v));
    float scale = 1.f;
    if (!o.raw && peak > 0.98f) {   // snd.py: scale down only when the peak exceeds 0.98
        scale = 0.98f / peak;
        for (float& v : st) v *= scale;
    }
    write_wav(path, st, kRate);
    std::printf("%s: %.3f s, peak %.4f (scale %.4f), voices peak %d, missing notes %d, render speed %.0fx real time\n", path.c_str(),
                double(st.size() / 2) / kRate, peak, scale, s.peak_voices(), s.missing_notes(), rt);
}

int compare(const std::string& a, const std::string& b, double seconds) {
    const Wav ra = read_wav(a), rb = read_wav(b);
    if (ra.channels != rb.channels || ra.rate != rb.rate) { std::fprintf(stderr, "format mismatch\n"); return 1; }
    size_t n = std::min(ra.data.size(), rb.data.size());
    if (seconds > 0) n = std::min(n, size_t(seconds * ra.rate) * size_t(ra.channels));
    double sa = 0, sb = 0, sab = 0, sd = 0, maxd = 0;
    size_t maxi = 0;
    for (size_t i = 0; i < n; ++i) {
        const double x = ra.data[i], y = rb.data[i];
        sa += x * x; sb += y * y; sab += x * y;
        sd += (x - y) * (x - y);
        if (std::fabs(x - y) > maxd) { maxd = std::fabs(x - y); maxi = i; }
    }
    std::printf("max diff at %.4f s (ref %.5f test %.5f)\n", double(maxi / size_t(ra.channels)) / ra.rate, double(ra.data[maxi]), double(rb.data[maxi]));
    {   // worst 1 s windows
        const size_t win = size_t(ra.rate) * size_t(ra.channels);
        std::vector<std::pair<double, size_t>> w;
        for (size_t s0 = 0; s0 < n; s0 += win) {
            double e = 0;
            const size_t e1 = std::min(n, s0 + win);
            for (size_t i = s0; i < e1; ++i) e += double(ra.data[i] - rb.data[i]) * double(ra.data[i] - rb.data[i]);
            w.emplace_back(std::sqrt(e / double(e1 - s0)), s0 / win);
        }
        std::sort(w.begin(), w.end(), [](auto& x, auto& y) { return x.first > y.first; });
        std::printf("worst 1 s windows (rms diff @ second):");
        for (size_t i = 0; i < std::min<size_t>(5, w.size()); ++i) std::printf(" %.5f@%zu", w[i].first, w[i].second);
        std::printf("\n");
    }
    const double rms_a = std::sqrt(sa / double(n)), rms_b = std::sqrt(sb / double(n)), rms_d = std::sqrt(sd / double(n));
    const double corr = sab / std::sqrt(sa * sb + 1e-30);
    std::printf("compare %.2f s (%zu frames; lengths %.3f / %.3f s): rms ref %.5f test %.5f, rms diff %.6f (%.3f%% of ref, %.1f dB), max diff %.5f, correlation %.6f\n",
                double(n / size_t(ra.channels)) / ra.rate, n / size_t(ra.channels), double(ra.data.size() / size_t(ra.channels)) / ra.rate,
                double(rb.data.size() / size_t(rb.channels)) / rb.rate, rms_a, rms_b, rms_d, 100.0 * rms_d / (rms_a + 1e-30),
                20.0 * std::log10(rms_d / (rms_a + 1e-30) + 1e-30), maxd, corr);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 4 && std::string(argv[1]) == "compare") return compare(argv[2], argv[3], argc > 4 ? std::atof(argv[4]) : 0.0);
        if (argc < 4) {
            std::fprintf(stderr, "usage: dcsound <iso> bgm <n> <seconds> <out.wav> | amb <set> <idx> <seconds> <out.wav> | se <set> <id> <out.wav> | special <n> <out.wav>  [options]\n"
                                 "       dcsound compare <ref.wav> <test.wav> [seconds]\n");
            return 2;
        }
        std::vector<std::string> pos;
        Opts o;
        for (int i = 3; i < argc; ++i) {
            const std::string a = argv[i];
            auto next = [&]() { if (i + 1 >= argc) throw Error("missing value for " + a); return std::string(argv[++i]); };
            if (a == "--game") o.game = true;
            else if (a == "--raw") o.raw = true;
            else if (a == "--noreverb") o.noreverb = true;
            else if (a == "--reverb") o.reverb = true;
            else if (a == "--flags") o.flags = true;
            else if (a == "--diffusion") o.diffusion = true;
            else if (a == "--limiter") o.limiter = true;
            else if (a == "--voices") o.voices = std::atoi(next().c_str());
            else if (a == "--chunk") o.chunk = std::max(1, std::atoi(next().c_str()));
            else if (a == "--seq") o.seq = std::atoi(next().c_str());
            else if (a == "--loops") o.loops = std::atoi(next().c_str());
            else if (a == "--hold") o.hold = std::atof(next().c_str());
            else if (a == "--voice") o.voice = std::atoi(next().c_str());
            else if (a == "--special") o.special = std::atoi(next().c_str());
            else pos.push_back(a);
        }
        const std::string cmd = argv[2];
        Disc disc{std::filesystem::path(argv[1])};
        SoundSystem s(disc);
        configure(s, o);
        double rt = 0;

        if (cmd == "bgm" || cmd == "amb") {
            const bool bgm = cmd == "bgm";
            if (pos.size() < (bgm ? 3u : 4u)) throw Error("usage: dcsound <iso> " + cmd + " <n> " + (bgm ? "" : "<idx> ") + "<seconds> <out.wav>");
            const int n = std::atoi(pos[0].c_str());
            const int idx = bgm ? o.seq : std::atoi(pos[1].c_str());
            const double seconds = std::atof(pos[bgm ? 1 : 2].c_str());
            const std::string out = pos[bgm ? 2 : 3];
            s.set_bgm_loop_limit(o.loops);
            if (bgm) { s.load_bgm(n); } else { s.load_sound_set(n); }
            if (o.noreverb) s.set_reverb_enabled(false);
            if (bgm) s.play_bgm(idx); else s.play_ambient(idx);
            std::vector<float> st;
            if (seconds > 0) {
                st = render_until(s, size_t(seconds * kRate), size_t(seconds * kRate), o.chunk, [] { return true; }, &rt);
            } else {
                // once through: until the sequence has ended and every voice is silent (bgm: the reverb tail is cut with the last voice, like snd.py)
                st = render_until(s, 0, size_t(900) * kRate, o.chunk, [&] { return s.active_voices() == 0 && !(bgm ? s.bgm_playing() : s.ambient_playing()); }, &rt);
            }
            finish(st, out, o, s, rt);
        } else if (cmd == "se") {
            if (pos.size() < 3) throw Error("usage: dcsound <iso> se <set> <id> <out.wav>");
            s.load_sound_set(std::atoi(pos[0].c_str()));
            if (o.voice >= 0) s.load_voice(o.voice);
            if (o.special >= 0) s.load_special(o.special);
            if (!o.reverb) s.set_reverb_enabled(false);
            const int id = std::atoi(pos[1].c_str());
            const auto info = s.se_info(id);
            if (!info) { std::fprintf(stderr, "SE %d has no table entry\n", id); return 1; }
            std::printf("SE %d -> program %d key %d port %d volume %d\n", id, info->prog, info->key, info->port, s.se_volume(id));
            s.play_se(id);
            double hold = o.hold;
            if (hold < 0 && s.se_is_looping(id)) hold = 1.0;
            std::vector<float> st;
            if (hold >= 0) {
                st = render_until(s, size_t(hold * kRate), size_t(hold * kRate), o.chunk, [] { return true; });
                s.stop_se(id);
            }
            std::vector<float> tail = render_until(s, 0, size_t(60) * kRate, o.chunk, [&] { return s.active_voices() == 0; }, &rt);
            st.insert(st.end(), tail.begin(), tail.end());
            if (!o.reverb) trim_silence(st);
            finish(st, pos[2], o, s, rt);
        } else if (cmd == "special") {
            if (pos.size() < 2) throw Error("usage: dcsound <iso> special <n> <out.wav>");
            const int n = std::atoi(pos[0].c_str());
            s.load_special(n);
            s.set_reverb_enabled(false);
            if (!s.play_special(n)) { std::fprintf(stderr, "special SE %d has no table entry\n", n); return 1; }
            std::vector<float> st = render_until(s, 0, size_t(60) * kRate, o.chunk, [&] { return s.active_voices() == 0; }, &rt);
            trim_silence(st);
            finish(st, pos[1], o, s, rt);
        } else if (cmd == "stress") {
            // thread-safety smoke test: an audio thread renders continuously while this thread loads banks and fires commands
            const double secs = pos.empty() ? 10.0 : std::atof(pos[0].c_str());
            std::atomic<bool> run{true}, bad{false};
            std::atomic<long> frames{0};
            std::thread audio([&] {
                std::vector<float> buf(1024);
                while (run) {
                    s.render(buf.data(), 512);
                    for (float f : buf)
                        if (!std::isfinite(f) || std::fabs(f) > 4.f) bad = true;
                    frames += 512;
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            });
            std::mt19937 rng(1234);
            const int sets[] = {0, 1, 100, 101, 105, 19, 300};
            const int bgms[] = {1, 100, 150, 4, 7};
            const auto t0 = std::chrono::steady_clock::now();
            int iter = 0;
            while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < secs) {
                const int r = int(rng() % 12);
                if (r == 0) s.load_sound_set(sets[rng() % 7]);
                else if (r == 1) { s.load_bgm(bgms[rng() % 5]); s.play_bgm(0); }
                else if (r == 2) s.load_voice(int(rng() % 7));
                else if (r == 3) s.load_special(1 + int(rng() % 60));
                else if (r == 4) s.play_special(int(rng() % 60));
                else if (r == 5) s.bgm_fade_out(30);
                else if (r == 6) s.bgm_fade_in(30);
                else if (r == 7) s.play_ambient(0);
                else if (r == 8) s.stop_bgm(20);
                else if (r == 9) s.stop_se(int(rng() % 600));
                else s.play_se_at(int(rng() % 600), Vec3{float(rng() % 400), 0.f, float(rng() % 400)});
                for (int k = 0; k < 4; ++k) s.play_se(int(rng() % 600), 0.5f, 0.3f);
                ++iter;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            run = false;
            audio.join();
            std::printf("stress: %d iterations, %ld frames rendered, voices peak %d, %s\n", iter, frames.load(), s.peak_voices(), bad ? "BAD SAMPLES" : "ok");
            return bad ? 1 : 0;
        } else {
            throw Error("unknown command " + cmd);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
