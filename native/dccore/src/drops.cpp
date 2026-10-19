#include "dccore/drops.h"
#include "dccore/gamemath.h"
#include "dccore/items.h"
#include "dccore/vu0.h"

namespace dccore {

void Drops::set(const float pos[3], int gold, int item) {
    for (Drop& e : d) {
        if (e.used) continue;
        e = Drop{};
        e.used = true;
        e.pos[0] = pos[0];
        e.pos[1] = gm::fadd(pos[1], 1.75f);
        e.pos[2] = pos[2];
        e.gold = gold;
        e.item = item;
        return;
    }
}

void Drops::step() {
    constexpr float kPi = 3.1415927410125732f, kSpin = 0.05235987901687622f, kRate = 0.10471975803375244f, kFade = 2.5132741928100586f;
    spin = gm::fadd(spin, kSpin);
    if (kPi < spin) spin = gm::fsub(spin, kPi);
    for (Drop& e : d) {
        if (!e.used) continue;
        if (e.state == 2) {
            e.timer = gm::fadd(e.timer, kRate);
            if (kFade < e.timer) e.used = false;
        } else if (e.state == 0) {
            e.timer = gm::fadd(e.timer, kRate);
            if (kPi < e.timer) {
                e.state = 1;
                e.timer = 0.0f;
            }
        }
    }
}

int Drops::check_position(const float prev[3], const UserStatus& status) {
    int total = 0;
    for (Drop& e : d) {
        if (!e.used || e.state != 1) continue;
        const float dist = vu0::dist(e.pos, prev);
        if (0 < e.block && 8.5f <= dist) e.block = 0;
        if (e.block == 0 && dist <= 5.0f) {
            if (e.gold == -1 && e.item != -1) {
                const int r = check_item_get(status, e.item);
                if (r == 0) {
                    picked.push_back(e.item);
                    e.used = false;
                } else {
                    e.block = r;
                }
            } else {
                e.state = 2;
                total += e.gold;
            }
        }
    }
    return total;
}

}  // namespace dccore
