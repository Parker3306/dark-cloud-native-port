#pragma once
// What a playable character needs from the disc: the model pack (dun/mainchara/cNN.chr), the equipped weapon's model (commenu/weapon/cNNwMM.chr) and the weapon's
// hit points (the objects dcol0 / dcol1 of the weapon model), plus the motion table of the character (base.cfg KEY lines).
#include <memory>
#include <string>
#include <vector>
#include "dccore/character.h"
#include "dccore/disc.h"
#include "dccore/motion_state.h"
#include "dccore/shots.h"

namespace dccore {

constexpr int kDefWeapon[6] = {257, 299, 314, 331, 347, 363};   // defWeapon: the first weapon id of each character (the one every character starts without)

std::string character_pack_path(int character);                  // charaNameTbl: "dun/mainchara/c01d.chr", ...
std::string weapon_pack_path(int character, int weapon_id);      // BtGetWeaponNamePath3: "commenu/weapon/c06w01.chr" (null id -> the first weapon)

struct PlayerKit {
    int character = 0;
    std::shared_ptr<Character> model;
    std::shared_ptr<Character> weapon_model;
    std::string weapon_path;
    float tip[3] = {0, 0, 0};    // dcol1 in the weapon's own space
    float tip2[3] = {0, 0, 0};   // dcol0
    std::vector<MotionInfo> motions;
    // The weapon's main effect (NowMainEffect: Goro's hammer wave, Ungaga's twister, ...): its definition row and model; null when the character has none ported.
    bool has_main_fx = false;
    ShotDef main_fx;
    std::shared_ptr<Character> main_fx_model;
    int main_fx_row = 0;
    int osmond_type = 0;   // DAT_01dc4520: 0 machine gun, 1 hand cannon (one strong shot), 2 flame thrower
};

// `element` is the weapon's element choice (UserStatus weapon +0x16); `dun_bin` is the dungeon overlay (DUN.BIN), which holds the effect rows.
PlayerKit load_player_kit(const Disc& disc, const Bytes& dun_bin, int character, int weapon_id, int element);

}  // namespace dccore
