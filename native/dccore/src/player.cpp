#include "dccore/player.h"
#include <cstdio>
#include <cstring>
#include "dccore/mds.h"
#include "dccore/pack.h"

namespace dccore {

std::string character_pack_path(int character) {
    static const char* kNames[6] = {"c01d", "c04b", "c06b", "c05a", "c10b", "c18a"};
    return std::string("dun/mainchara/") + kNames[character < 0 || character > 5 ? 0 : character] + ".chr";
}

std::string weapon_pack_path(int character, int weapon_id) {
    static const char* kPrefix[6] = {"c01w", "c04w", "c06w", "c05w", "c10w", "c18w"};
    const int ch = character < 0 || character > 5 ? 0 : character;
    int index = weapon_id - kDefWeapon[ch];
    if (index < 0 || index > 99) index = 1;
    char buf[64];
    std::snprintf(buf, sizeof buf, "commenu/weapon/%s%02d.chr", kPrefix[ch], index);
    return buf;
}

PlayerKit load_player_kit(const Disc& disc, const Bytes& dun_bin, int character, int weapon_id, int element) {
    PlayerKit k;
    k.character = character;
    k.model = std::make_shared<Character>(load_character(Pack(disc.read(character_pack_path(character)))));
    for (const MotionEntry& m : k.model->motions) k.motions.push_back({int(m.start), int(m.end), m.speed});
    k.weapon_path = weapon_pack_path(character, weapon_id);
    if (!disc.contains(k.weapon_path)) k.weapon_path = weapon_pack_path(character, kDefWeapon[character < 0 || character > 5 ? 0 : character] + 1);
    Pack wp(disc.read(k.weapon_path));
    k.weapon_model = std::make_shared<Character>(load_character(wp));
    for (const Joint& j : k.weapon_model->joints) {   // (the world position in the weapon's rest pose: the hit points can hang below other objects)
        if (j.name == "dcol1") for (int i = 0; i < 3; ++i) k.tip[i] = float(j.world[12 + i]);
        if (j.name == "dcol0") for (int i = 0; i < 3; ++i) k.tip2[i] = float(j.world[12 + i]);
    }
    // Get_Main_EffectPtr: the variable (in the dungeon overlay) that points at the row of the effect
    uint32_t var = 0;
    switch (character) {
        case 0: var = 0x01DC21F0; break;
        case 1: var = 0x01DC21F4; break;
        case 2: var = 0x01DC21F8; break;
        case 3: var = 0x01DC21FC + 4u * uint32_t(element < 0 || element >= 5 ? 0 : element); break;
        case 4: var = 0x01DC2210; break;
        case 5: {   // Osmond: the weapon decides both the effect row and how it is used (OpB_InitProcess2)
            int row = 0;
            switch (weapon_id) {
                case 0x16d: row = 3; break;
                case 0x170: case 0x171: row = 0; k.osmond_type = 2; break;
                case 0x172: row = 4; break;
                case 0x173: row = 1; k.osmond_type = 1; break;
                case 0x174: row = 5; break;
                case 0x175: case 0x176: row = 2; k.osmond_type = 1; break;
                default: break;
            }
            var = 0x01DC2214 + 4u * uint32_t(row);
            break;
        }
        default: break;
    }
    if (var != 0 && var - ShotDef::kOverlayBase + 4 <= dun_bin.size()) {
        uint32_t row = 0;
        std::memcpy(&row, dun_bin.data() + (var - ShotDef::kOverlayBase), 4);
        if (row >= ShotDef::kOverlayBase && row - ShotDef::kOverlayBase + 0x70 <= dun_bin.size()) {
            k.main_fx = ShotDef::from_memory(dun_bin.data() + (row - ShotDef::kOverlayBase));
            k.main_fx_row = int((row - 0x01DC1B60) / 0x70);
            const std::string path = "dun/mainchara/wep_eff/" + k.main_fx.name + ".chr";
            if (disc.contains(path)) {
                k.main_fx_model = std::make_shared<Character>(load_character(Pack(disc.read(path))));
                k.has_main_fx = true;
            }
        }
    }
    return k;
}

}  // namespace dccore
