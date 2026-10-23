#include "dccore/fishing.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "dccore/libm_ps2.h"
#include "dccore/walk.h"

namespace dccore {

namespace {
constexpr uint32_t kFishInfo = 0x296050, kEsaInfo = 0x26ae90, kFishMsg = 0x292aa0;
constexpr float kPi = 3.14159265358979f;
float angle_limit(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a <= -kPi) a += 2.0f * kPi;
    return a;
}
}  // namespace

FishTables FishTables::load(const Elf& elf) {
    FishTables t;
    for (int k = 0; k < 18; ++k) {
        const uint32_t at = kFishInfo + uint32_t(k) * 0x48;
        FishInfo& f = t.fish[k];
        f.ref_len = elf.get<float>(at);
        f.min_len = elf.get<float>(at + 4);
        f.max_len = elf.get<float>(at + 8);
        f.fp_min = elf.get<int32_t>(at + 12);
        f.fp_max = elf.get<int32_t>(at + 16);
        for (int b = 0; b < 13; ++b) f.attract[b] = elf.get<float>(at + 20 + uint32_t(b) * 4);
        f.message = int(elf.get<uint8_t>(kFishMsg + uint32_t(k)));
    }
    for (int b = 0; b < 13; ++b) {
        t.bait_item[b] = elf.get<int32_t>(kEsaInfo + uint32_t(b) * 8);
        t.bait_radius[b] = elf.get<float>(kEsaInfo + uint32_t(b) * 8 + 4);
    }
    for (int i = 0; i < 35; ++i) {
        t.exchange[size_t(i)][0] = elf.get<int16_t>(0x2929d0 + uint32_t(i) * 4);
        t.exchange[size_t(i)][1] = elf.get<int16_t>(0x2929d0 + uint32_t(i) * 4 + 2);
    }
    return t;
}

float FishingSim::nrnd() {
    float s = 0.0f;
    for (int i = 0; i < 12; ++i) s += rnd();
    return s - 6.0f;
}

void FishingSim::set_scale(Fish& f) {
    const FishInfo& info = tables_->fish[std::clamp(f.kind, 0, 17)];
    const float r = nrnd();
    float len = r >= 0.0f ? info.min_len + r * (info.max_len - info.min_len) / 4.0f : info.min_len + r * (info.max_len - info.min_len) / 8.0f;
    len = std::clamp(len, 0.5f * info.min_len, info.max_len);
    f.len = len;
    f.scale = len / info.ref_len;
}

void FishingSim::load(int set, int period) {
    static constexpr int kN[4] = {50, 20, 35, 25};
    const int n = kN[std::clamp(period, 0, 3)];
    fish_num = (set == 0 || set == 4) ? 4 : 5;
    for (int i = 0; i < 6; ++i) fish[size_t(i)] = Fish();
    for (int i = 0; i < fish_num; ++i) {
        Fish& f = fish[size_t(i)];
        int kind = 1;
        switch (set) {
            case 0: {
                static constexpr int k0[4] = {1, 2, 6, 7};
                kind = k0[std::clamp(int(3.99999f * rnd()), 0, 3)];
                break;
            }
            case 1: {
                const int r = int(100.0f * rnd());
                kind = r < 35 ? 1 : (r < 70 ? 4 : (r < 80 ? 9 : 10));
                break;
            }
            case 3: {
                const int r = int(rng_->next() % 100u);
                kind = r < 20 ? 0 : (r < 40 ? 3 : (r < 60 ? 11 : (r < 80 ? 12 : 13)));
                break;
            }
            default: {   // sets 2 and 4: a rare slot (Mardan / Baron Garayan), otherwise the pond's own fish
                if (int(rng_->next() % uint32_t(n)) == 0) {
                    kind = (rng_->next() % 5u) == 0 ? 17 : 5;
                } else if (set == 2) {
                    static constexpr int k2[3] = {2, 4, 6};
                    kind = k2[rng_->next() % 3u];
                } else {
                    const int r = int(rng_->next() % 100u);
                    kind = r < 40 ? 14 : (r < 70 ? 15 : 16);
                }
            }
        }
        f.kind = kind;
        set_scale(f);
    }
}

void FishingSim::init_area(float x0, float z0, float x1, float z1) {
    fish_area[0] = x0; fish_area[1] = z0; fish_area[2] = x1; fish_area[3] = z1;
    for (int i = 0; i < fish_num; ++i) {
        Fish& f = fish[size_t(i)];
        f.pos[0] = (x0 + x1) * 0.5f;
        f.pos[1] = water_y - 12.0f;
        f.pos[2] = (z0 + z1) * 0.5f;
        f.bite = -1;
        f.move_mode = 0;
        f.speed = 0;
    }
}

void FishingSim::release_all() {
    for (int i = 0; i < fish_num; ++i) {
        Fish& f = fish[size_t(i)];
        f.bite = -1;
        f.move_mode = 0;
        f.state_timer = 0;
    }
}

void FishingSim::step_fish(Fish& f, const float hook[3]) {
    const FishInfo& info = tables_->fish[std::clamp(f.kind, 0, 17)];
    const float bait_pos[3] = {hook[0], hook[1] - 3.0f, hook[2]};
    const float radius = bait >= 0 ? tables_->bait_radius[bait] : 0.0f;
    const float attract = bait >= 0 ? info.attract[bait] : 0.0f;
    const float dx = bait_pos[0] - f.pos[0], dy = bait_pos[1] - f.pos[1], dz = bait_pos[2] - f.pos[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (f.move_mode != 0xb) {
        // ---- the bite state machine
        if (f.bite == -1 || f.state_timer < 0) {
            f.state_timer = int(rng_->next() % 30u) + 20;
            switch (f.bite) {
                case -1:
                    if (bait >= 0 && radius > 0.0f && dist <= radius && rand_check(0.3f * attract)) f.bite = 6;
                    break;
                case 6: {
                    const bool eat = dist <= 1.0f && rand_check(0.5f * attract);
                    const bool leave = bait < 0 || rand_check(0.1f);
                    if (eat) f.bite = 7;
                    if (leave) { f.bite = 9; f.state_timer /= 2; }
                    break;
                }
                case 7: {
                    const bool bite_it = dist <= 1.0f && rand_check(0.5f * attract);
                    const bool leave = rand_check(0.1f);
                    if (bite_it) f.bite = 8;
                    if (leave) { f.bite = 9; f.state_timer /= 2; }
                    break;
                }
                case 8: f.bite = 9; f.state_timer *= 2; break;
                case 9: f.bite = -1; break;
                default: break;
            }
        }
        --f.state_timer;
    }
    // ---- where it swims
    if (f.move_mode == 0xb) {
        if (--f.move_timer < 0) {
            f.move_timer = int(rng_->next() % 100u) + 20;
            f.target_yaw = angle_limit(2.0f * kPi * rnd());
            f.target_speed = 0.5f;
        }
    } else if (f.bite == 6) {
        f.target_yaw = ps2_atan2f(dx, dz);
        f.target_speed = dist < 1.0f ? 0.0f : 0.1f;
    } else if (f.bite == 7) {
        f.target_yaw = ps2_atan2f(dx, dz);
        f.target_speed = 0.0f;
    } else if (f.bite == 8) {
        f.target_yaw = ps2_atan2f(dx, dz) + 0.2f * (rnd() - 0.5f) * 2.0f;
        f.target_speed = 0.0f;
    } else if (f.bite == 9) {
        f.target_yaw = angle_limit(ps2_atan2f(dx, dz) + kPi);
        f.target_speed = 0.5f;
    } else if (--f.move_timer < 0) {
        f.move_timer = int(rng_->next() % 100u) + 20;
        if (!f.moved_once) { f.move_mode = 1; f.moved_once = true; }
        else f.move_mode = int((rng_->next() >> 15) & 1u);   // (the game's rand() & 1; the low bit of the LCG alternates with every call, so when a fish's decisions always take an even number of draws it never changes its mind: the game has many other consumers of rand() that break the pattern, this port has only the fish)
        if (f.move_mode == 1) {
            f.move_timer += 30;
            f.target_yaw = angle_limit(2.0f * kPi * rnd());
            f.target_speed = 0.2f + 0.5f * rnd() / 2.0f;
        } else {
            f.target_speed = 0.0f;
        }
    }
    f.yaw = angle_interpolate(f.yaw, f.target_yaw, f.move_mode == 0xb ? 0.1f : 0.05f, 0);
    const float ramp = f.move_mode == 0xb ? 0.02f : 0.01f;
    if (f.speed < f.target_speed) f.speed = std::min(f.target_speed, f.speed + ramp);
    else if (f.speed > f.target_speed) f.speed = std::max(f.target_speed, f.speed - ramp);
    f.pos[0] += std::sin(f.yaw) * f.speed;
    f.pos[2] += std::cos(f.yaw) * f.speed;
    f.pos[1] = water_y - 12.0f;
    // the fish stay in their part of the pond
    const float lo_x = std::min(fish_area[0], fish_area[2]), hi_x = std::max(fish_area[0], fish_area[2]);
    const float lo_z = std::min(fish_area[1], fish_area[3]), hi_z = std::max(fish_area[1], fish_area[3]);
    if (f.pos[0] < lo_x || f.pos[0] > hi_x) { f.pos[0] = std::clamp(f.pos[0], lo_x, hi_x); f.target_yaw = angle_limit(-f.yaw); }
    if (f.pos[2] < lo_z || f.pos[2] > hi_z) { f.pos[2] = std::clamp(f.pos[2], lo_z, hi_z); f.target_yaw = angle_limit(kPi - f.yaw); }
    f.motion = (f.bite == 8 || f.move_mode == 0xb) ? 1 : (f.move_mode == 0 ? 6 : (f.move_mode == 1 ? 0 : 1));
}

void FishingSim::step(const float hook[3]) {
    for (int i = 0; i < fish_num; ++i) step_fish(fish[size_t(i)], hook);
}

int FishingSim::status(int* index) const {
    int hooked = -1, nibbling = -1;
    for (int i = 0; i < fish_num; ++i) {
        if (fish[size_t(i)].bite == 8 && hooked < 0) hooked = i;
        if (fish[size_t(i)].bite == 7 && nibbling < 0) nibbling = i;
    }
    if (hooked >= 0) { if (index) *index = hooked; return 8; }
    if (nibbling >= 0) { if (index) *index = nibbling; return 7; }
    return -1;
}

FishCatch FishingSim::catch_of(int i) const {
    FishCatch c;
    const Fish& f = fish[size_t(std::clamp(i, 0, 5))];
    const FishInfo& info = tables_->fish[std::clamp(f.kind, 0, 17)];
    c.kind = f.kind;
    c.size10 = int(f.len * 10.0f);
    float fp;
    if (f.len >= info.min_len) fp = float(info.fp_min) + float(info.fp_max - info.fp_min) * (f.len - info.min_len) / (info.max_len - info.min_len);
    else fp = float(info.fp_min) - (float(info.fp_min) * (info.min_len - f.len) / (info.min_len - 0.5f * info.min_len)) * 0.5f;
    c.points = int(fp);
    return c;
}

// ---- bookkeeping -------------------------------------------------------------------------------------------------------------------------------------------

int fishing_points(const SaveData& s) { return s.get<int32_t>(SaveData::kFishingPoint); }
void set_fishing_points(SaveData& s, int fp) { s.set<int32_t>(SaveData::kFishingPoint, std::clamp(fp, 0, 9999)); }
void add_fishing_points(SaveData& s, int fp) { set_fishing_points(s, fishing_points(s) + fp); }

namespace {
constexpr size_t kRankBase = 0x1e0;
}

void set_fishing_rank(SaveData& s, int size10, int kind) {
    int slot = -1;
    for (int i = 0; i < 64; ++i)
        if (s.get<float>(kRankBase + size_t(i) * 16 + 4) <= 0.0f) { slot = i; break; }
    if (slot < 0) {   // the smallest record is overwritten without comparing (the game's own habit)
        float best = 1e30f;
        for (int i = 0; i < 64; ++i) {
            const float v = s.get<float>(kRankBase + size_t(i) * 16 + 4);
            if (v < best) { best = v; slot = i; }
        }
    }
    s.set<int32_t>(kRankBase + size_t(slot) * 16, kind);
    s.set<float>(kRankBase + size_t(slot) * 16 + 4, float(size10));
    for (int pass = 0; pass < 64; ++pass)   // bubble sort, the biggest first
        for (int i = 0; i + 1 < 64; ++i) {
            const size_t a = kRankBase + size_t(i) * 16, b = a + 16;
            if (s.get<float>(a + 4) < s.get<float>(b + 4)) {
                uint8_t tmp[16];
                std::memcpy(tmp, s.raw.data() + a, 16);
                std::memcpy(s.raw.data() + a, s.raw.data() + b, 16);
                std::memcpy(s.raw.data() + b, tmp, 16);
            }
        }
}

bool fishing_rank(const SaveData& s, int index, int& kind, int& size10) {
    if (index < 0 || index >= 64) return false;
    const float size = s.get<float>(kRankBase + size_t(index) * 16 + 4);
    if (size <= 0.0f) return false;
    kind = s.get<int32_t>(kRankBase + size_t(index) * 16);
    size10 = int(size);
    return true;
}

int mardan_caught(const SaveData& s) { return s.get<int32_t>(0x12f94); }
int mardan_counter(const SaveData& s) { return s.game_int(0x14); }

void register_catch(SaveData& s, const FishCatch& c) {
    if (c.kind == 5 || c.kind == 17) {
        s.set<int32_t>(0x12f94, s.get<int32_t>(0x12f94) + 1);
        s.set_game_int(0x14, std::clamp(s.game_int(0x14) + 1, 0, 10000));
    }
    add_fishing_points(s, c.points);
    set_fishing_rank(s, c.size10, c.kind);
}

}  // namespace dccore
