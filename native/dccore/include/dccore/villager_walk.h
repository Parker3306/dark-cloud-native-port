#pragma once
// Villagers that roam a Georama town: the random walk on the edit grid (EdSetVillagerNextPos + CNPCharacter::PlaySeq) and the choice of a start cell
// (GetRandomPlanePos). Rules: notes/villagers.md sections 3 and 4. Positions are the town's world coordinates; one grid cell is 100 units.
#include <array>
#include <vector>
#include "dccore/gamemath.h"
#include "dccore/town.h"

namespace dccore {

struct Roamer {
    bool roamer = false;          // walks (the others stand at their point)
    bool present = false;         // outdoors and visible
    float speed = 0.3f;           // units per tick
    float pos[3] = {0, 0, 0};
    float yaw = 0;
    int command = 0;              // 0 none (decide next), 1 walking to `target`, 2 waiting
    int wait = 0;                 // ticks left of a wait
    float target[3] = {0, 0, 0};
    bool moving = false;          // the last step moved the villager (walk animation) rather than idling
};

// GetRandomPlanePos: a flat free or road cell of the town (cells 2 .. size-3 of an edit area), within `radius` of `center` (radius <= 0: anywhere) and at least `avoid_r` from
// every avoid point. False when there is none.
bool roam_random_plane_pos(const Town& t, const float center[3], float radius, const std::vector<std::array<float, 3>>& avoid, float avoid_r, GameRand& rng, float out[3]);

// EdSetVillagerNextPos: when villager `self` has nothing to do it picks the next cell (or waits one second). `all` are the town's roamers (for the "somebody walks there" test).
void roam_decide(const Town& t, std::vector<Roamer>& all, size_t self, GameRand& rng);

// CNPCharacter::PlaySeq for one tick: walks toward the target (or counts a wait down), turning at 0.1 rad per tick.
void roam_step(Roamer& r);

}  // namespace dccore
