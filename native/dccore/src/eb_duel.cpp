#include "dccore/eb_duel.h"
#include <algorithm>
#include <cmath>

namespace dccore {

void EbDuel::init(float arg) {
    log.push_back("init " + std::to_string(arg));
    count_ = 0;
    keys_.clear();
    end_count_ = 0;
    key_count_ = 0;
    finish_cnt_ = 0;
    speed_ = arg * 4.0f;
    fade_bgm = true;
    play_fanfare = true;
    ok_timer_ = 0;
    ok_key_ = -1;
    // (the motion list and eb_cool_flag are not reset by EBInit)
}

void EbDuel::set_motions(const std::vector<EbMotion>& m) {
    log.push_back("set_motions " + std::to_string(m.size()));
    motions_ = m;
    end_count_ = 0;
    for (EbMotion& e : motions_) {
        e.ticks = int(std::trunc((e.end - e.start) / e.speed));
        end_count_ += e.ticks;
    }
    if (motions_.empty()) return;   // a missing motion: the duel does not start and the loop answers 1 at once
    active_ = true;
    cool_flag_ = true;
}

void EbDuel::set_key(float t, uint32_t mask, int level) {
    if (int(keys_.size()) >= kMaxKeys) return;
    int sum = 0, tick = -1;
    for (const EbMotion& e : motions_) {
        if (e.start <= t && t < e.end) { tick = int(std::trunc(float(sum) + (t - e.start) / e.speed)); break; }
        sum += e.ticks;
    }
    if (tick < 0) tick = sum;   // a time inside no motion ends up as the total
    log.push_back("key t=" + std::to_string(t) + " tick " + std::to_string(tick) + " mask " + std::to_string(mask));
    EbKey k;
    k.tick = tick;
    k.mask = mask;
    k.level = std::min(level, 5);
    keys_.push_back(k);
}

int EbDuel::step(uint32_t pad_down) {
    if (!active_) return 1;
    if (finish_cnt_ >= 1) {
        if (finish_cnt_ == 1) { exit(); return result_; }
        --finish_cnt_;
        return 0;
    }
    if (ok_timer_ > 0) --ok_timer_;
    const float S = speed_;
    int cand = -1;
    bool good = false;
    for (size_t i = 0; i < keys_.size(); ++i) {
        EbKey& k = keys_[i];
        const float d = float(count_ - k.tick) * S;
        k.hint = false;
        int mine = int(i);
        if (d > 0.0f) {
            if (d >= 48.0f) { k.passed = true; mine = cand; }
            if (d < 24.0f) good = true;
        } else {
            if (d <= -16.0f) mine = cand;
            if (k.level > 0 && d <= 16.0f - float(6 - k.level) * 64.0f) k.hint = true;
        }
        cand = mine;
    }
    bool err = false;
    if (cand < 0) {
        if (pad_down != 0) err = true;
    } else {
        EbKey& k = keys_[size_t(cand)];
        k.pressed |= pad_down;
        if (k.pressed == k.mask) {
            if (!k.done) {
                sounds_.push_back({EbSound::Se, good ? 10 : 9});
                ok_timer_ = 30;
                ok_good_ = good;
                ok_key_ = cand;
                if (!good) cool_flag_ = false;
                ++key_count_;
            }
            k.done = true;
        }
        if (k.mask != (k.mask | k.pressed)) err = true;   // a bit that is not part of the mask
    }
    for (size_t i = 0; i < keys_.size() && keys_[i].passed; ++i)
        if (keys_[i].mask != keys_[i].pressed) err = true;   // a key whose window ended unfinished
    if (err && key_count_ < int(keys_.size())) {   // (after the last hit nothing can fail any more)
        log.push_back("MISS at count " + std::to_string(count_) + " pad " + std::to_string(pad_down));
        sounds_.push_back({EbSound::BgmFade, 0x28});
        finish_cnt_ = 0x50;
        result_ = -1;
        ++count_;
        return 0;
    }
    if (count_ == end_count_ - 100 && fade_bgm) sounds_.push_back({EbSound::BgmFade, 100});
    if (count_ < end_count_) {
        ++count_;
        return 0;
    }
    if (play_fanfare) sounds_.push_back({EbSound::Fanfare, 47});
    log.push_back("end reached count " + std::to_string(count_));
    finish_cnt_ = 0xa0;
    result_ = cool_flag_ ? 2 : 1;
    return 0;
}

void EbDuel::intro_start() {
    log.push_back("intro_start");
    intro_cnt_ = 0;
    intro_flag_ = true;
}

int EbDuel::intro_step() {
    if (!intro_flag_) return 1;
    ++intro_cnt_;
    if (intro_cnt_ > 100) return 1;
    if (intro_cnt_ % 8 == 0) sounds_.push_back({EbSound::Se, 8});
    return 0;
}

void EbDuel::exit() {
    log.push_back("exit");
    active_ = false;
    intro_cnt_ = 0;
    intro_flag_ = false;
    finish_cnt_ = 0;
    ok_timer_ = 0;
}

}  // namespace dccore
