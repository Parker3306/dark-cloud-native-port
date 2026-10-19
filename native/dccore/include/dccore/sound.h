#pragma once
// Dark Cloud sound engine (pure C++, no engine/audio-API dependency). Port of tools/dc/snd.py (the executable spec); format and runtime model: notes/sound.md.
//
//   .snd pack  -> SndPack      (bank .hd/.bd -> Bank, sequence .sq -> Song, load list .txt, reverb/TABLE .cfg)
//   SoundSystem                (mirrors the game's Snd* layer: load_bgm/play_bgm/..., load_sound_set/play_se/..., render() = 48 kHz stereo float)
//
// Threading: every SoundSystem command may be called from the game thread while another thread calls render(); commands are queued and applied at the
// start of the next render() call. Loading (disc read, parse, ADPCM pre-decode) is synchronous in the calling thread and never touches the audio thread.
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "dccore/bytes.h"
#include "dccore/disc.h"
#include "dccore/elf.h"
#include "dccore/pack.h"

namespace dccore::snd {

constexpr int kRate = 48000;  // SPU2 output rate; a VAG of rate R plays at R/48000 source samples per output sample

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// PS-ADPCM
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct Pcm {
    std::vector<int16_t> data;
    int32_t loop_start = 0;   // sample index of the first block carrying the loop-start flag (0 when none)
    bool looping = false;     // the END block carries the repeat flag
    bool truncated = false;   // the data ended without an END block
};
// Decodes 16-byte blocks from `start` up to and including the first block with the END flag (bit 0). Identical to snd.adpcm_decode.
Pcm adpcm_decode(View bd, size_t start);

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Banks (.hd + .bd, Sony libhsyn): program -> split (key range) -> sample set (velocity range) -> sample -> VAG
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct VagInfo { uint32_t offset = 0xFFFFFFFF; uint16_t rate = 0; uint8_t flags = 0; bool valid = false; };
struct Smpl {
    bool valid = false;
    uint16_t vag = 0;
    uint8_t vel_lo = 0, vel_hi = 0, base_key = 60, pan = 64, group = 0, priority = 10, volume = 128, spu_flags = 0;
    int8_t fine = 0;           // 1/128 semitone
    uint16_t adsr1 = 0, adsr2 = 0;
};
struct Sset { bool valid = false; uint8_t vel_curve = 0, vel_lo = 0, vel_hi = 127; std::vector<uint16_t> smpl; };
struct Split {
    uint16_t sset = 0xFFFF;
    uint8_t lo = 0, hi = 0;
    uint16_t bend_lo = 0, bend_hi = 0;   // units of 1/256 semitone at full bend
    uint8_t vol = 128, pan = 64;
    int8_t transpose = 0, detune = 0;
    int8_t pan_key_amount = 0;           // split bytes 14/15 (key-follow of the pan, as read by the IOP note-on code)
    uint8_t pan_key_center = 60;
};
struct Program {
    bool valid = false;
    uint8_t vol = 128, pan = 64, pan_key_center = 60;
    int8_t transpose = 0, detune = 0, pan_key_amount = 0;
    std::vector<Split> splits;
};

class Bank {
public:
    // hd/bd may be empty (placeholder bank). Throws Error on a malformed header. All VAGs are ADPCM-decoded here.
    static std::shared_ptr<Bank> parse(View hd, View bd, std::string name = "");

    struct Layer { const Split* split; const Sset* sset; const Smpl* smpl; const VagInfo* vag; const Pcm* pcm; const Program* prog; };
    // Every sample a note-on (program, key, velocity) starts (layers are summed). Returns the count written to out (<= max).
    int resolve(int prog, int key, int vel, Layer* out, int max) const;

    std::string name;
    bool empty = true;
    std::vector<VagInfo> vagi;
    std::vector<Smpl> smpl;
    std::vector<Sset> sset;
    std::vector<Program> prog;
    std::vector<Pcm> pcm;   // per VAG index (empty data for unused slots)
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Sequences (.sq, Sony libmidi): Format-0 MIDI in a compact coding, with the Sony loop controllers (CC99/CC6/CC38)
// ---------------------------------------------------------------------------------------------------------------------------------------------------
enum EvKind : uint8_t { EvOn, EvOff, EvCc, EvPc, EvBend, EvTempo, EvEnd };
struct Ev { uint32_t tick; uint8_t kind, ch, a, b; uint32_t val; };   // bend: a = lsb, b = msb; tempo: val = microseconds per quarter
struct Song {
    std::string name;
    int division = 480;
    std::vector<Ev> events;
    static Song parse(View data, std::string name = "", int index = 0);
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// .snd pack = generic pack with .hd .bd .sq .txt .cfg entries
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct SongEntry { char letter; std::string name; std::shared_ptr<const Song> song; int volume; };   // volume: sqtbl.txt (default 100)
struct SongSet { std::vector<SongEntry> songs; };

class SndPack {
public:
    // stem = file name without extension ("bgm1"); selects <stem>.txt/.cfg (falls back to the first .txt/.cfg entry)
    SndPack(Bytes data, const std::string& stem);
    std::vector<std::string> load_list;
    std::optional<std::pair<int, int>> reverb;   // REVERBE mode,depth
    std::optional<std::pair<int, int>> table;    // TABLE basic,cap
    // Banks named by the load list (`.hd` names), keyed by the letter before the extension, parsed and decoded.
    std::vector<std::pair<char, std::shared_ptr<Bank>>> banks() const;
    // Sequences in load-list order (empty and missing ones skipped); `volumes` = sqtbl.txt table (may be null).
    std::vector<SongEntry> songs(const std::unordered_map<std::string, int>* volumes = nullptr) const;
    const Pack& pack() const { return pack_; }
private:
    Pack pack_;
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// Tables (executable data segment + sound/tbl/*.txt)
// ---------------------------------------------------------------------------------------------------------------------------------------------------
struct SeEntry { int8_t prog = -1, key = 0, flag = 0, port = -1; };
struct SeTable { bool present = false; std::vector<SeEntry> e; };
struct SndTables {
    std::vector<SeEntry> se_info;                 // 2800
    std::vector<SeTable> cap, voice, basic;       // 101, 11, 2
    std::vector<SeEntry> special;                 // 64
    std::unordered_map<int, int> setbl_vol;       // (prog << 8 | key & 0xFF) -> volume
    std::unordered_map<std::string, int> sq_vol;  // lower-case sequence name -> port volume
    static SndTables load(const Elf& elf, const Disc& disc);
    // GetSeInfo: nullopt = no table entry / program < 0.
    std::optional<SeEntry> lookup(int se_id, int basic_no, int cap_no, int voice_set) const;
    int volume_of(int prog, int key) const;       // setbl.txt volume, 0x40 when the pair is not listed
};

struct Vec3 { float x = 0, y = 0, z = 0; };

// ---------------------------------------------------------------------------------------------------------------------------------------------------
// SoundSystem
// ---------------------------------------------------------------------------------------------------------------------------------------------------
using SeHandle = uint32_t;   // 0 = nothing played

class SoundSystem {
public:
    explicit SoundSystem(const Disc& disc);   // reads the SE tables from the executable and sound/tbl/{setbl,sqtbl}.txt
    ~SoundSystem();
    SoundSystem(const SoundSystem&) = delete;
    SoundSystem& operator=(const SoundSystem&) = delete;

    // ---- audio thread: OVERWRITES out[0 .. 2*frames) with 48 kHz interleaved stereo float. Real-time capable (no allocation, no blocking: commands are
    // taken with try_lock, so a command posted while render() runs is applied by the next call).
    void render(float* stereo_interleaved, int frames);

    // ---- music (SndBgm*)
    void load_bgm(int n);                      // sound/bgm/bgm<n>.snd: bank A + sequences, SetReverb(core 0). Stops the current BGM.
    void play_bgm(int index = 0);              // SQ_Play(0,i): port volume = sqtbl.txt value of the sequence
    void stop_bgm(int fade_frames = 0);        // SndBgmStop / SndBgmFadeOutStop (fade in 60 Hz frames)
    void pause_bgm();
    void resume_bgm();                         // SndBgmRePlay
    void set_bgm_volume(int v);                // SndSetBgmVol: 0..127, only while playing
    void set_bgm_volume_f(float f);            // SndSetBgmVolf: f * default volume
    void bgm_fade_in(int frames, int target = -1, int from = 0);   // SndBgmFadeIn (target -1 = default volume)
    void bgm_fade_out(int frames, int target = 0);                 // SndBgmFadeOut
    bool bgm_fading() const { return bgm_fading_.load(); }
    bool bgm_playing() const { return bgm_playing_.load(); }
    bool ambient_playing() const { return amb_playing_.load(); }
    int bgm_volume() const { return bgm_vol_.load(); }
    int bgm_default_volume() const { return bgm_default_vol_; }
    int bgm_number() const { return bgm_no_; }
    int bgm_sequence_count() const;
    void set_bgm_loop_limit(int n);            // -1 (default, like the game) = endless loops; n = follow the loop n more times, then play the tail and stop

    // ---- sound sets, ambient sequences
    // The opening's scene packs hold a bank set and the music in one pack (LoadSoundFileFromPack("o01a.txt")): music on port 0, effects banks on the others.
    void load_music_pack(Bytes pack, const std::string& stem);
    SeHandle play_pk(int port, int prog, int key, float volf = -1.f);   // one note of a bank on a port (the opening's SE command)
    void stop_pk(int prog, int key);
    void load_sound_set(int n);                // sound/set/snd<n>.snd: banks C E I G M, E sequences, TABLE, SetReverb(core 1); stops all SE
    int sound_set() const { return sound_set_; }
    void play_ambient(int index = 0);          // SQ_Play(1,i)
    void stop_ambient();
    void set_ambient_volume(int v);            // port volume units (gain = v/256)
    void set_ambient_volume_f(float f);
    int ambient_sequence_count() const;

    // ---- sound effects (SndSePlay & co). id = the game's SE id (tables via the loaded sound set / voice set).
    SeHandle play_se(int id, float volf = -1.f, float panf = 0.f);      // volf < 0: setbl.txt volume; else clamp(setbl * volf, 0, 127). panf in [-1,1]
    SeHandle play_se_at(int id, Vec3 pos, float near_d = 20.f, float far_d = 500.f);   // SndGetVolPan (distance/pan from the listener)
    SeHandle play_se_timed(int id, int frames, float volf = -1.f, float panf = 0.f);   // SndSeSeqPlayStop: stop after `frames` 60 Hz frames
    void stop_se(int id);                      // SE_Stop: note-off of every running voice of that SE id (ADSR release)
    void stop_se_handle(SeHandle h);
    void set_se_volume(int id, float volf);    // retarget running voices (SndSetSeVolf)
    void set_se_pan(int id, float panf);       // SndSetSePanf
    void stop_all_se();
    void set_listener(Vec3 pos, Vec3 dir);     // SndSetCamera
    // Table lookups (game-thread side, no audio effect)
    struct SeInfo { int prog, key, port; int volume; };
    std::optional<SeInfo> se_info(int id) const;
    bool se_is_looping(int id) const;          // any layer of the SE is a looping sample (needs stop_se)
    int se_volume(int id, float volf = -1.f) const;

    // ---- voices (SndVoice*): voice/voice<c>.snd, S bank on port 11; ids 400..499 use voice table c
    void load_voice(int character);
    int voice_set() const { return voice_set_; }
    // ---- special SE (SndSP*): special/sp<n>.snd, Q bank on port 12
    void load_special(int n);
    SeHandle play_special(int index, float volf = -1.f, float panf = 0.f);
    void stop_special();

    // ---- mixer settings
    void set_master_volume(float v);
    void set_stereo(bool stereo);              // false = mono downmix
    void set_reverb_enabled(bool on);          // default on
    void set_reverb_send_all(bool on);         // true = every voice feeds the reverb (what tools/dc/snd.py does); default false = per-sample routing flags
    void set_reverb_diffusion(bool on);        // allpass diffusion after the comb filters (default on; off = exactly snd.py's reverb)
    void set_reverb_presets(bool on);          // delay/gain scaling by the cfg reverb mode 2/3/4 (default on; off = snd.py's single preset)
    void set_limiter(bool on);                 // soft clip above 0.9 (default on)
    void set_voice_limit(int core0, int core1);// simultaneous voices per SPU2 core (hardware: 24)
    // ---- state (updated by render())
    int active_voices() const { return active_.load(); }
    int peak_voices() const { return peak_.load(); }
    int missing_notes() const { return missing_.load(); }   // note-ons that found no sample
    double bgm_position() const { return bgm_pos_.load(); } // seconds of sequence time since play_bgm

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> bgm_fading_{false}, bgm_playing_{false}, amb_playing_{false};
    std::atomic<int> bgm_vol_{0}, active_{0}, peak_{0}, missing_{0};
    std::atomic<double> bgm_pos_{0};
    int bgm_default_vol_ = 0, bgm_no_ = -1, sound_set_ = -1, voice_set_ = -1;
    friend struct Impl;
};

}  // namespace dccore::snd
