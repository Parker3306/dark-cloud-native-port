// Co-op balance (coop_scale.h): the curves, and that the pool scales only what can hurt the player.
#include <cmath>
#include <cstdio>
#include "dccore/combat.h"
#include "dccore/coop_scale.h"

using namespace dccore;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
    // hit points: (n + 1) / 2
    if (!near(coop_hp_mult(1), 1.0f) || !near(coop_hp_mult(2), 1.5f) || !near(coop_hp_mult(3), 2.0f) || !near(coop_hp_mult(4), 2.5f) || !near(coop_hp_mult(9), 2.5f)) return 1;
    // damage / spawn / drops: 10 % per extra player below four, 50 % with four
    if (!near(coop_curve(1), 1.0f) || !near(coop_curve(2), 1.1f) || !near(coop_curve(3), 1.2f) || !near(coop_curve(4), 1.5f) || !near(coop_curve(0), 1.0f)) return 2;
    // monsters on a random floor: 15 and 8 at most 50 % more
    if (coop_spawn_count(15, 1) != 15 || coop_spawn_count(15, 4) != 22 || coop_spawn_count(8, 4) != 12 || coop_spawn_count(15, 2) != 16 || coop_spawn_count(15, 3) != 18) return 3;
    ColPool pool;
    pool.monster_damage_mult = coop_damage_mult(4);
    const float p[3] = {0, 0, 0};
    const int mon = pool.set(5, 0, p, 10, 1, 1, 0, 0, 0);    // target 1: hurts the player
    const int own = pool.set(5, 0, p, 10, 1, 2, 0, 0, 0);    // target 2: the player's own sword
    if (pool.e[size_t(mon)].damage != 15 || pool.e[size_t(own)].damage != 10) return 4;
    ColPool plain;
    if (plain.e[size_t(plain.set(5, 0, p, 10, 1, 1, 0, 0, 0))].damage != 10) return 5;   // one player: exactly the game's numbers
    std::printf("coop scale ok\n");
    return 0;
}
