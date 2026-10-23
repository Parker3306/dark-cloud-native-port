#pragma once
// Co-op balance: how a floor grows with the number of players (2..4). The original game has no such rule, the numbers follow Diablo 2 / Dungeon Siege (hit points) and the
// project owner's choice (notes/coop_native.md, "Balance").
//   hit points: base * (n + 1) / 2                          (150 % with 2 players, 200 % with 3, 250 % with 4)
//   damage the monsters deal, monster count, drops: base * curve(n),  curve = 1.1 / 1.2 / 1.5 for 2 / 3 / 4 players
#include <algorithm>

namespace dccore {

inline int coop_clamp_players(int n) { return std::clamp(n, 1, 4); }
inline float coop_hp_mult(int n) { return float(coop_clamp_players(n) + 1) * 0.5f; }
inline float coop_curve(int n) {
    n = coop_clamp_players(n);
    return n >= 4 ? 1.5f : 1.0f + 0.1f * float(n - 1);
}
inline float coop_damage_mult(int n) { return coop_curve(n); }
inline float coop_drop_mult(int n) { return coop_curve(n); }
// monsters put on a random floor: 15 on the main floor, 8 on the back floor (more is capped by MonsterUnit::kSlots)
inline int coop_spawn_count(int base, int n) { return int(float(base) * coop_curve(n)); }

}  // namespace dccore
