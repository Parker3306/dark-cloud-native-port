// dcweapon <iso>: tries the weapon rules (attach, level up, status break, build-up targets) on a new game's first weapon.
#include <cstdio>
#include "dccore/disc.h"
#include "dccore/text.h"
#include "dccore/weapon_ops.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dcweapon <iso>\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        Elf elf(disc.read_root("SCUS_971.11"));
        const ItemDb db = ItemDb::load(elf);
        const MessageBank names = MessageBank::parse(disc.read("meswin/_systeme.bin"));
        auto name = [&](int id) { return names.get(db.name_message(id), {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"}); };
        set_weapon_rules(&db);
        UserStatus s = UserStatus::new_game();
        GameRand rng;
        rng.seed(3);
        auto show = [&](const char* what) {
            const WeaponView w = s.weapon(0);
            const WeaponView b = s.stored_weapon(0);
            std::printf("%-22s %s lv %d  atk %d(%d) end %d(%d) spd %d(%d) mag %d(%d) whp %d exp %d/%d elem [%d %d %d %d %d] flags %x\n", what, name(w.id()).c_str(), w.level(), w.attack(), b.attack(), s.get<int16_t>(0x450c + 6), 0, w.recharge(), b.recharge(), w.magic(), b.magic(), w.max_durability(), w.exp(), db.weapon_max_exp(b.p),
                        w.element_value(0), w.element_value(1), w.element_value(2), w.element_value(3), w.element_value(4), w.status_flags());
        };
        show("start");
        for (int id : {0x5b, 0x51, 0x5f, 0x5c}) get_item(s, db, id, rng, 3);
        std::printf("attach 0x5b -> hole 0: %d\n", int(weapon_attach(s, db, 0, 0, 0, 0)));
        show("with attack chip");
        std::printf("attach fire -> hole 1: %d\n", int(weapon_attach(s, db, 0, 0, 1, 1)));
        show("with fire");
        s.set<int16_t>(0x450c + 0x14, 99);   // plenty of exp
        std::printf("level up: %d\n", int(weapon_level_up(s, db, rng, 0, 0, false, false)));
        show("after level up");
        std::printf("status break: %d\n", int(weapon_status_break(s, db, rng, 0, 0)));
        show("after status break");
        for (const BuildTarget& t : build_up_targets(s, db, rng, 0, 0)) std::printf("  build-up target %s %s\n", name(t.id).c_str(), t.buildable ? "(buildable)" : "");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
