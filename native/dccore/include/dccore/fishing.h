#pragma once
// Fishing (GameMode 0x10 of the towns): the fish and bait tables, the fish of a pond (which kinds live where, size, fishing points, the bite behaviour of every
// fish), and the bookkeeping of a catch (fishing points, the record list, Mardan Garayan). The hook position comes from the front end. Rules: notes/fishing.md.
#include <array>
#include <string>
#include "dccore/elf.h"
#include "dccore/dungeon.h"
#include "dccore/gamemath.h"
#include "dccore/savedata.h"

namespace dccore {

struct FishInfo {
    float ref_len = 1, min_len = 1, max_len = 1;
    int fp_min = 0, fp_max = 0;
    float attract[13] = {};   // how much the fish wants each bait
    int message = 0;          // name: message `message + 30` of the system text (FishMsg)
};

struct FishTables {
    FishInfo fish[18];
    int bait_item[13] = {};
    float bait_radius[13] = {};
    std::array<std::array<int, 2>, 35> exchange{};   // exitemlst: {item id, fishing points}
    static FishTables load(const Elf& elf);
    static std::string model_file(int kind) { char b[40]; std::snprintf(b, sizeof b, "chara/f%02da.chr", kind + 1); return b; }
};

struct Fish {
    int kind = -1;
    float len = 0;                 // length in the game's units; the size shown is int(len * 10)
    float scale = 1;
    float pos[3] = {0, 0, 0};
    float yaw = 0, speed = 0, target_yaw = 0, target_speed = 0;
    int move_mode = 0;             // 0 stand / glide, 1 swim, 10 on the hook, 0xb in the fight
    int bite = -1;                 // -1 none, 6 interested in the bait, 7 nibbling, 8 hooked, 9 leaving, 0xb the fight
    int move_timer = 0, state_timer = 0;
    bool moved_once = false;
    int motion = 6;
};

struct FishCatch {
    int kind = -1;
    int size10 = 0;                // int(len * 10)
    int points = 0;                // fishing points of this fish
};

class FishingSim {
public:
    FishingSim(const FishTables* tables, GameRand* rng) : tables_(tables), rng_(rng) {}

    std::array<Fish, 6> fish;
    int fish_num = 5;
    float water_y = 0, ground_y = -30;
    float fish_area[4] = {0, 0, 0, 0};   // x0, z0, x1, z1 where the fish are kept
    int bait = -1;                       // the bait index in use (-1 none: nothing bites)

    // FishingLoadFish: the fish of pond `set` (0 Norune, 1 / 2 the Matataki ponds, 3 the story pond, 4 Queens) at the part of the day `period` (0..3)
    void load(int set, int period);
    // FishingInitFish: every fish at the centre of the area
    void init_area(float x0, float z0, float x1, float z1);
    // one tick: `hook` is where the bait hangs
    void step(const float hook[3]);
    // FishingFishStatus: 8 when a fish is hooked, 7 when one nibbles, -1 otherwise; `index` the fish
    int status(int* index) const;
    // the catch of fish `i`: its record
    FishCatch catch_of(int i) const;
    // After the fight: the fish goes back, the pond starts over
    void release_all();

    float rnd() { return gm::fdiv(gm::itof(rng_->next()), gm::kRandRange); }

private:
    const FishTables* tables_;
    GameRand* rng_;
    bool rand_check(float p) { return (rng_->next() % 1000) < int(p * 1000.0f); }
    void step_fish(Fish& f, const float hook[3]);
    void set_scale(Fish& f);
    float nrnd();
};

// ---- bookkeeping in the save data (notes/fishing.md section 3)
int fishing_points(const SaveData& s);
void add_fishing_points(SaveData& s, int fp);
void set_fishing_points(SaveData& s, int fp);
// SetFishingRank: the 64-slot record list at +0x1e0 ({int kind, float size, 8 bytes})
void set_fishing_rank(SaveData& s, int size10, int kind);
bool fishing_rank(const SaveData& s, int index, int& kind, int& size10);
// What a catch does to the save data: fishing points, the record list, and Mardan / Baron Garayan (kinds 5 and 17): the lifetime count and the build-up counter (game int 0x14)
void register_catch(SaveData& s, const FishCatch& c);
int mardan_caught(const SaveData& s);       // SaveData + 0x12f94
int mardan_counter(const SaveData& s);      // game int 0x14

}  // namespace dccore
