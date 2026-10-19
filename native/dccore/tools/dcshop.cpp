// dcshop <iso>: prints every shop's stock with prices and runs a few buy / sell / store / take operations on a new game's record.
#include <cstdio>
#include "dccore/disc.h"
#include "dccore/shop.h"
#include "dccore/text.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: dcshop <iso>\n"); return 2; }
    try {
        Disc disc{std::filesystem::path(argv[1])};
        Elf elf(disc.read_root("SCUS_971.11"));
        const ShopTables shops = ShopTables::load(elf);
        const ItemDb db = ItemDb::load(elf);
        const MessageBank names = MessageBank::parse(disc.read("meswin/_systeme.bin"));
        auto name = [&](int id) { return names.get(db.name_message(id), {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"}); };
        for (int s = 0; s < ShopTables::kShops; ++s) {
            std::printf("shop %d:", s);
            for (int id : shops.stock(s)) std::printf(" %s(%d/%d)", name(id).c_str(), shops.buy_price(id), shops.sell_price(id));
            std::printf("\n");
        }
        UserStatus st = UserStatus::new_game();
        SaveData save;
        GameRand rng;
        rng.seed(7);
        st.set<uint16_t>(kGoldOff, 1000);
        std::printf("gold %d\n", gold(st));
        std::printf("buy bread -> %d, gold %d\n", int(shop_buy(st, db, shops, 0, 0, rng)), gold(st));
        std::printf("buy gold bullion x2 -> %d %d, gold %d\n", int(shop_buy(st, db, shops, 0, 19, rng)), int(shop_buy(st, db, shops, 0, 19, rng)), gold(st));
        std::printf("buy attack chip (300) -> %d, gold %d\n", int(shop_buy(st, db, shops, 0, 11, rng)), gold(st));
        ItemRef bag{0, 0, 0};
        ShopResult why;
        std::printf("sell bag slot 0 (%s) price %d\n", name(item_ref_id(st, bag)).c_str(), sale_price(st, db, shops, bag, &why));
        std::printf("store -> %d, stored items %d\n", int(storage_store(st, save, db, bag)), storage_count(save, 0));
        std::printf("take -> %d, bag id %d\n", int(storage_take(st, save, db, 0, 0, rng)), item_ref_id(st, bag));
        ItemRef w{1, 0, 0};
        std::printf("sell Toan's dagger -> %d (expect refusal)\n", int(shop_sell(st, db, shops, w)));
        std::printf("sell bag slot 0 -> %d, gold %d\n", int(shop_sell(st, db, shops, bag)), gold(st));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
