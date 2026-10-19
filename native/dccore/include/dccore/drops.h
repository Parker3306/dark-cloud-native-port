#pragma once
// Things monsters leave behind (CRandomItem): up to 32 pieces of gold or items that hop out, lie on the floor and are collected when the player comes within 5 units.
#include <array>
#include <cstdint>
#include <vector>
#include "dccore/user_status.h"

namespace dccore {

struct Drop {
    bool used = false;          // +0x290 != -1
    float pos[4] = {0, 0, 0, 1};
    int gold = -1;              // +0x310: amount of gold, -1 for an item
    int item = -1;              // +0x594: item id, -1 for gold
    int state = 0;              // +0x390: 0 hopping out, 1 lying there, 2 collected (fades)
    float timer = 0;            // +0x410: animation clock; the drawn height is pos.y + sin(timer) * 8
    int block = 0;              // +0x514: set when the bag was full: the item is ignored until the player steps away
};

class Drops {
public:
    static constexpr int kSize = 32;
    std::array<Drop, kSize> d;
    float spin = 0;             // +0x490: the turning of the item icons
    std::vector<int> picked;    // item ids the player picked up since the last call to take_picked()

    void set(const float pos[3], int gold, int item);                  // Set__11CRandomItem
    void step();                                                       // Step__11CRandomItem
    int check_position(const float player_prev_pos[3], const UserStatus& status);   // returns the gold collected; items go to `picked`
    std::vector<int> take_picked() { std::vector<int> r; r.swap(picked); return r; }
};

}  // namespace dccore
