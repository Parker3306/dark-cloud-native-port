#pragma once
// Monsters of a dungeon floor: the models/scripts a floor uses (SetupBaseModel), and where they stand (ArrangementPos).
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "dccore/character.h"
#include "dccore/disc.h"
#include "dccore/dungeon.h"
#include "dccore/script.h"
#include "dccore/tables.h"
#include "dccore/events.h"
#include "dccore/shots.h"

namespace dccore {

// One kind of monster loaded for the floor.
struct MonsterBase {
    int monster_id = -1;                          // index into GameTables::monsters
    MonsterEntry entry;
    std::shared_ptr<Character> model;             // dun/monstor/<stem>.chr
    std::vector<std::shared_ptr<Character>> extras;   // optional extra models (entry +0x10..+0x30)
    std::shared_ptr<ScriptProgram> script;        // dun/monstor/<script stem>.stb (null if the file is missing)
    // the monster's shot effects (entry +0x68 and +0x6a: rows of BtEntryEffectTbl; the models dun/effect/<name>.chr)
    std::array<int, 2> shot_def = {-1, -1};
    std::array<ShotDef, 2> shot;
    std::array<std::shared_ptr<Character>, 2> shot_model;
};

// One monster standing on the floor (a slot of CMonstorUnit; the game has 16).
struct MonsterInstance {
    bool active = false;
    int base = -1;                // index into MonsterUnit::bases
    float pos[3] = {0, 0, 0};
    float yaw = 0;
};

class MonsterUnit {
public:
    static constexpr int kSlots = 24;   // (the game has 16; co-op adds up to 50 % more monsters on a random floor, see coop_scale.h)
    std::vector<MonsterBase> bases;
    std::array<MonsterInstance, kSlots> slots;

    // BtLoadMonstor: loads the models and scripts listed for (map, floor) in the enemy layout table (up to 9, in order; a monster id of -1 ends the list).
    void load_floor(const Disc& disc, const GameTables& tables, int map_no, int floor, bool back_floor);
    // The same for an explicit list of monster ids (rows of MonstorTable): tests and tools.
    void load_ids(const Disc& disc, const GameTables& tables, const std::vector<int>& ids);

    // BtArrengeMonstor -> ArrangementPos: puts `count` monsters on random free spots (at most 3 monsters within 480 units of each other, none within 25 of another,
    // none within 20 of a treasure box); kinds other than 0 and 3 are used once per call. `which` < 0 picks the model at random.
    // `on_place(slot)` is called right after each monster is put down (SetupViewMonstor runs the monster's init script there, which can move it and use rand()).
    void arrange(const FloorLayout& layout, const GameTables& tables, int map_no, GameRand& rng, int count = 15, int which = -1, const std::function<void(int)>& on_place = {}, const FloorEvents* events = nullptr);

    int active_count() const;
    std::vector<std::string>* arrange_log = nullptr;   // when set, every candidate position of arrange() is described here (for debugging against the game)
};

}  // namespace dccore
