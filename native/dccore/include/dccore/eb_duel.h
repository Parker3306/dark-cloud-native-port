#pragma once
// The EB duel ("event battle", ebattle.cpp of the game): a rhythm mini-game that runs over a cut-scene. Toan and his opponent play a pre-authored motion queue while
// markers scroll along a lane; the player has to press the shown button(s) when a marker is inside the window. No health, no choices: every key hit in its window =
// CLEAR (all hits in the inner window = EXCELLENT), any error = MISS. Rules: notes/eb_duel.md.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dccore {

struct EbMotion {
    int no = -1;
    float start = 0, end = 0, speed = 1;
    int ticks = 0;                 // trunc((end - start) / speed)
};

struct EbKey {
    int tick = 0;
    uint32_t mask = 0;             // raw pad bits (0x10 triangle, 0x20 circle, 0x40 cross, 0x80 square, 0x1000 up, 0x2000 right, 0x4000 down, 0x8000 left)
    int level = 0;
    uint32_t pressed = 0;
    bool done = false, passed = false, hint = false;
};

// what the loop asks the front end to play
struct EbSound {
    enum Kind { Se, BgmFade, Fanfare } kind = Se;
    int a = 0;                     // Se: system SE id (9 hit, 10 cool hit, 8 intro); BgmFade: ticks
};

class EbDuel {
public:
    static constexpr int kMaxKeys = 64;

    void init(float arg);                                    // _EB_INIT(speed arg): resets the counters; markers move 4 * arg pixels per tick
    // _EB_SET_MOTION: the motions (cfg rows) of the hero's timeline; the duel only starts when there is at least one
    void set_motions(const std::vector<EbMotion>& m);
    void set_key(float t, uint32_t mask, int level);         // _EB_SET_KEY: `t` is motion time, converted to a tick
    int step(uint32_t pad_down);                             // _EB_LOOP: 0 running, 1 clear, 2 excellent, -1 miss (also 1 when the duel is not active)
    void intro_start();                                      // _EB_INTRO_START
    int intro_step();                                        // _EB_INTRO_LOOP: 1 on the 101st call
    void exit();                                             // _EB_INTRO_END / the end of the result phase
    bool active() const { return active_; }                  // the pad treats the stick as a d-pad while this is on
    bool intro_active() const { return intro_flag_; }
    std::vector<EbSound> take_sounds() { std::vector<EbSound> s; s.swap(sounds_); return s; }
    std::vector<std::string> log;                            // what the script asked for (tests)
    bool play_fanfare = true, fade_bgm = true;               // _EB_FINISH_SOUND switches them off

    // what the overlay draws
    float speed() const { return speed_; }
    int count() const { return count_; }
    int intro_count() const { return intro_cnt_; }
    int finish_count() const { return finish_cnt_; }
    int result() const { return result_; }
    int key_count() const { return key_count_; }             // keys hit so far
    const std::vector<EbKey>& keys() const { return keys_; }
    int popup_timer() const { return ok_timer_; }
    bool popup_good() const { return ok_good_; }
    int popup_key() const { return ok_key_; }

private:
    bool active_ = false, intro_flag_ = false;
    float speed_ = 4.0f;
    int count_ = 0, intro_cnt_ = 0, finish_cnt_ = 0, end_count_ = 0, result_ = 0, key_count_ = 0;
    bool cool_flag_ = true;
    std::vector<EbMotion> motions_;
    std::vector<EbKey> keys_;
    std::vector<EbSound> sounds_;
    int ok_timer_ = 0, ok_key_ = -1;
    bool ok_good_ = false;
};

}  // namespace dccore
