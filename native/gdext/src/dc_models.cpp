#include "dc_models.h"
#include "dc_pad.h"
#include <cstdlib>
#include "dc_opening.h"

#include <fstream>
#include <map>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include "dc_character.h"
#include "dc_dungeon.h"
#include "dc_walker.h"
#include "dc_sound.h"
#include "dc_town.h"
#include "dc_townwalk.h"
#include <dccore/dungeon.h>
#include <dccore/dungeon_script.h>
#include <dccore/status_ops.h>
#include <dccore/floor.h>
#include <dccore/monsters.h>
#include <dccore/character.h>
#include <dccore/pack.h>
#include <dccore/tim2.h>
#include <dccore/mds.h>

using namespace godot;

void DcModels::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open_iso", "path"), &DcModels::open_iso);
    ClassDB::bind_method(D_METHOD("has_file", "game_path"), &DcModels::has_file);
    ClassDB::bind_method(D_METHOD("get_file_count"), &DcModels::get_file_count);
    ClassDB::bind_method(D_METHOD("build_character", "game_path"), &DcModels::build_character);
    ClassDB::bind_method(D_METHOD("build_character_file", "disk_path"), &DcModels::build_character_file);
    ClassDB::bind_method(D_METHOD("build_dungeon", "name", "seed", "rooms", "map_no"), &DcModels::build_dungeon);
    ClassDB::bind_method(D_METHOD("make_town_model", "pack_path", "cfg", "color"), &DcModels::make_town_model, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("make_player_model", "character", "color", "weapon_id"), &DcModels::make_player_model, DEFVAL(-1), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("build_walker", "name", "seed", "rooms", "map_no", "character", "floor", "monsters", "arrange_seed"), &DcModels::build_walker, DEFVAL(0), DEFVAL(true), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("get_last_info"), &DcModels::get_last_info);
    ClassDB::bind_method(D_METHOD("new_game", "seed"), &DcModels::new_game);
    ClassDB::bind_method(D_METHOD("set_coop_floor", "seed", "host_status"), &DcModels::set_coop_floor);
    ClassDB::bind_method(D_METHOD("set_coop_players", "n"), &DcModels::set_coop_players);
    ClassDB::bind_method(D_METHOD("pad_state"), &DcModels::pad_state);
    ClassDB::bind_method(D_METHOD("clear_coop_floor"), &DcModels::clear_coop_floor);
    ClassDB::bind_method(D_METHOD("enter_dungeon_reset"), &DcModels::enter_dungeon_reset);
    ClassDB::bind_method(D_METHOD("get_status_bytes"), &DcModels::get_status_bytes);
    ClassDB::bind_method(D_METHOD("draw_coop_seed"), &DcModels::draw_coop_seed);
    ClassDB::bind_method(D_METHOD("start_floor", "name", "map_no", "floor", "rooms", "character"), &DcModels::start_floor, DEFVAL(6), DEFVAL(String("dun/mainchara/c01d.chr")));
    ClassDB::bind_method(D_METHOD("end_floor", "walker"), &DcModels::end_floor);
    ClassDB::bind_method(D_METHOD("get_session_status"), &DcModels::get_session_status);
    ClassDB::bind_method(D_METHOD("fish_baits"), &DcModels::fish_baits);
    ClassDB::bind_method(D_METHOD("fish_take_bait", "item"), &DcModels::fish_take_bait);
    ClassDB::bind_method(D_METHOD("fish_points"), &DcModels::fish_points);
    ClassDB::bind_method(D_METHOD("fish_set_points", "fp"), &DcModels::fish_set_points);
    ClassDB::bind_method(D_METHOD("fish_add_record", "kind", "size10"), &DcModels::fish_add_record);
    ClassDB::bind_method(D_METHOD("fish_exchange_rows"), &DcModels::fish_exchange_rows);
    ClassDB::bind_method(D_METHOD("fish_exchange", "row"), &DcModels::fish_exchange);
    ClassDB::bind_method(D_METHOD("fish_records"), &DcModels::fish_records);
    ClassDB::bind_method(D_METHOD("get_floor_map", "dungeon", "floor"), &DcModels::get_floor_map);
    ClassDB::bind_method(D_METHOD("set_clear_jump", "map", "event"), &DcModels::set_clear_jump);
    ClassDB::bind_method(D_METHOD("boss_cleared", "map_no", "floor"), &DcModels::boss_cleared);
    ClassDB::bind_method(D_METHOD("game_over", "walker"), &DcModels::game_over);
    ClassDB::bind_method(D_METHOD("cheat_set_game_int", "index", "value"), &DcModels::cheat_set_game_int);
    ClassDB::bind_method(D_METHOD("cheat_set_game_flag", "index", "value"), &DcModels::cheat_set_game_flag);
    ClassDB::bind_method(D_METHOD("get_party"), &DcModels::get_party);
    ClassDB::bind_method(D_METHOD("switch_character", "walker", "character"), &DcModels::switch_character);
    ClassDB::bind_method(D_METHOD("join_party", "character"), &DcModels::join_party);
    ClassDB::bind_method(D_METHOD("set_character", "character"), &DcModels::set_character);
    ClassDB::bind_method(D_METHOD("get_character"), &DcModels::get_character);
    ClassDB::bind_method(D_METHOD("cheat_give_item", "item"), &DcModels::cheat_give_item);
    ClassDB::bind_method(D_METHOD("get_bag"), &DcModels::get_bag);
    ClassDB::bind_method(D_METHOD("use_bag_item", "slot"), &DcModels::use_bag_item);
    ClassDB::bind_method(D_METHOD("take_bag_item", "slot"), &DcModels::take_bag_item);
    ClassDB::bind_method(D_METHOD("get_weapons"), &DcModels::get_weapons);
    ClassDB::bind_method(D_METHOD("equip_weapon", "slot"), &DcModels::equip_weapon);
    ClassDB::bind_method(D_METHOD("rest"), &DcModels::rest);
    ClassDB::bind_method(D_METHOD("get_deepest_floor", "dungeon"), &DcModels::get_deepest_floor);
    ClassDB::bind_method(D_METHOD("get_village_status"), &DcModels::get_village_status);
    ClassDB::bind_method(D_METHOD("save_game", "path"), &DcModels::save_game);
    ClassDB::bind_method(D_METHOD("load_game", "path"), &DcModels::load_game);
    ClassDB::bind_method(D_METHOD("get_item_name", "item"), &DcModels::get_item_name);
    ClassDB::bind_method(D_METHOD("build_sound"), &DcModels::build_sound);
    ClassDB::bind_method(D_METHOD("get_town_layout", "town"), &DcModels::get_town_layout);
    ClassDB::bind_method(D_METHOD("set_town_layout_override", "town", "layout"), &DcModels::set_town_layout_override);
    ClassDB::bind_method(D_METHOD("clear_town_layout_overrides"), &DcModels::clear_town_layout_overrides);
    ClassDB::bind_method(D_METHOD("build_town", "name", "complete", "start_event", "old_map"), &DcModels::build_town, DEFVAL(-1), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("build_interior", "town", "house", "start_event", "variant"), &DcModels::build_interior, DEFVAL(-1), DEFVAL(0));
    ClassDB::bind_method(D_METHOD("grant_atla", "town", "install"), &DcModels::grant_atla, DEFVAL(false));
    ClassDB::bind_method(D_METHOD("build_opening"), &DcModels::build_opening);
    ClassDB::bind_method(D_METHOD("set_gold", "gold"), &DcModels::set_gold);
    ClassDB::bind_method(D_METHOD("get_clear_jump"), &DcModels::get_clear_jump);
    ClassDB::bind_method(D_METHOD("peek_save", "path"), &DcModels::peek_save);
    ClassDB::bind_method(D_METHOD("set_last_town", "town"), &DcModels::set_last_town);
    ClassDB::bind_method(D_METHOD("get_last_town"), &DcModels::get_last_town);
    ClassDB::bind_method(D_METHOD("give_item", "id"), &DcModels::give_item);
    ClassDB::bind_method(D_METHOD("get_party_size"), &DcModels::get_party_size);
    ClassDB::bind_method(D_METHOD("menu_text", "id"), &DcModels::menu_text);
    ClassDB::bind_method(D_METHOD("article_text", "id"), &DcModels::article_text);
    ClassDB::bind_method(D_METHOD("wm_weapons", "ch", "walker"), &DcModels::wm_weapons, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_attach_items", "walker"), &DcModels::wm_attach_items, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_equip", "ch", "slot", "walker"), &DcModels::wm_equip, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_discard", "ch", "slot", "walker"), &DcModels::wm_discard, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_element", "ch", "slot", "element", "walker"), &DcModels::wm_element, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_attach", "ch", "slot", "hole", "attach_slot", "walker"), &DcModels::wm_attach, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_detach", "ch", "slot", "hole", "walker"), &DcModels::wm_detach, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_repair", "ch", "slot", "walker"), &DcModels::wm_repair, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_level_up", "ch", "slot", "walker"), &DcModels::wm_level_up, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_status_break", "ch", "slot", "walker"), &DcModels::wm_status_break, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_build_targets", "ch", "slot", "walker"), &DcModels::wm_build_targets, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_build_up", "ch", "slot", "target", "walker"), &DcModels::wm_build_up, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("wm_powder_count", "id", "walker"), &DcModels::wm_powder_count, DEFVAL(Variant()));
    ClassDB::bind_method(D_METHOD("get_option", "row"), &DcModels::get_option);
    ClassDB::bind_method(D_METHOD("set_option", "row", "value"), &DcModels::set_option);
    ClassDB::bind_method(D_METHOD("get_hero_name", "ch"), &DcModels::get_hero_name);
    ClassDB::bind_method(D_METHOD("set_hero_name", "ch", "name"), &DcModels::set_hero_name);
    ClassDB::bind_method(D_METHOD("shop_stock", "shop"), &DcModels::shop_stock);
    ClassDB::bind_method(D_METHOD("shop_buy", "shop", "index"), &DcModels::shop_buy);
    ClassDB::bind_method(D_METHOD("shop_inventory", "for_sale"), &DcModels::shop_inventory);
    ClassDB::bind_method(D_METHOD("shop_sell", "kind", "owner", "slot"), &DcModels::shop_sell);
    ClassDB::bind_method(D_METHOD("storage_list", "kind"), &DcModels::storage_list);
    ClassDB::bind_method(D_METHOD("storage_store", "kind", "owner", "slot"), &DcModels::storage_store);
    ClassDB::bind_method(D_METHOD("storage_take", "kind", "slot"), &DcModels::storage_take);
    ClassDB::bind_method(D_METHOD("storage_capacity", "kind"), &DcModels::storage_capacity);
    ClassDB::bind_method(D_METHOD("shop_text", "id"), &DcModels::shop_text);
    ClassDB::bind_method(D_METHOD("item_description", "id"), &DcModels::item_description);
    ClassDB::bind_method(D_METHOD("get_item_icon", "item"), &DcModels::get_item_icon);
    ClassDB::bind_method(D_METHOD("get_pack_texture", "pack_path", "texture_name"), &DcModels::get_pack_texture);
    ClassDB::bind_method(D_METHOD("get_system_text", "id"), &DcModels::get_system_text);
}

bool DcModels::open_iso(const String &path) {
    try {
        disc_ = std::make_unique<dccore::Disc>(std::filesystem::path(path.utf8().get_data()));
        return true;
    } catch (const std::exception &e) {
        disc_.reset();
        UtilityFunctions::push_error(String("DcModels.open_iso: ") + e.what());
        return false;
    }
}

bool DcModels::has_file(const String &game_path) const { return disc_ && disc_->contains(game_path.utf8().get_data()); }
int DcModels::get_file_count() const { return disc_ ? int(disc_->entries().size()) : 0; }

Node3D *DcModels::build_character(const String &game_path) {
    if (!disc_) { UtilityFunctions::push_error("DcModels: no ISO opened"); return nullptr; }
    try {
        dccore::Pack pack(disc_->read(game_path.utf8().get_data()));
        auto ch = std::make_shared<dccore::Character>(dccore::load_character(pack));
        last_info_.clear();
        DcCharacter *node = memnew(DcCharacter);
        node->setup(ch, last_info_);
        return node;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_character: ") + e.what());
        return nullptr;
    }
}

Node3D *DcModels::build_character_file(const String &disk_path) {
    try {
        std::ifstream f(std::filesystem::path(disk_path.utf8().get_data()), std::ios::binary);
        if (!f) throw dccore::Error("cannot open " + std::string(disk_path.utf8().get_data()));
        dccore::Bytes data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        dccore::Pack pack(std::move(data));
        auto ch = std::make_shared<dccore::Character>(dccore::load_character(pack));
        last_info_.clear();
        DcCharacter *node = memnew(DcCharacter);
        node->setup(ch, last_info_);
        return node;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_character_file: ") + e.what());
        return nullptr;
    }
}

Node3D *DcModels::build_dungeon(const String &name, int seed, int rooms, int map_no) {
    if (!disc_) { UtilityFunctions::push_error("DcModels: no ISO opened"); return nullptr; }
    try {
        const std::string stem = name.utf8().get_data();
        dccore::Bytes cfg = disc_->read("dun/" + stem + ".cfg");
        dccore::Pack pack(disc_->read("dun/mpd_pack/" + stem + ".mpd"));
        auto dungeon = std::make_shared<dccore::Dungeon>(dccore::load_dungeon(std::string(cfg.begin(), cfg.end()), pack));
        dccore::FloorLayout layout = dccore::build_random_map(uint32_t(seed), rooms, map_no);
        DcDungeon *node = memnew(DcDungeon);
        node->setup(dungeon, layout);
        last_info_ = node->get_info();
        return node;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_dungeon: ") + e.what());
        return nullptr;
    }
}

void DcModels::load_tables() {
    if (tables_) return;
    dccore::Elf elf(disc_->read_root("SCUS_971.11"));
    tables_ = std::make_shared<dccore::GameTables>(dccore::GameTables::load(elf));
    event_tables_ = std::make_shared<dccore::EventTables>(dccore::EventTables::load(elf));
    item_db_ = std::make_shared<dccore::ItemDb>(dccore::ItemDb::load(elf));
    system_text_ = std::make_shared<dccore::MessageBank>(dccore::MessageBank::parse(disc_->read("meswin/_systeme.bin")));
    geo_tables_ = std::make_shared<dccore::GeoTables>(dccore::GeoTables::load(elf));
    fish_tables_ = std::make_shared<dccore::FishTables>(dccore::FishTables::load(elf));
    shop_tables_ = std::make_shared<dccore::ShopTables>(dccore::ShopTables::load(elf));
    dccore::set_weapon_rules(item_db_.get());   // the combat reads the weapons with their attachments
    try {
        dccore::Pack emenu(disc_->read("commenu/a_usa/emenu.pak"));
        if (const dccore::PackEntry *e = emenu.find("allmenu.mes")) menu_text_ = std::make_shared<dccore::MessageBank>(dccore::MessageBank::parse(emenu.data(*e)));
    } catch (const std::exception &) {
    }
    try {   // the shop texts
        dccore::Pack shop(disc_->read("commenu/a_usa/itemshop.pak"));
        if (const dccore::PackEntry *e = shop.find("itemshop.bin")) shop_desc_ = std::make_shared<dccore::MessageBank>(dccore::MessageBank::parse(shop.data(*e)));
        if (const dccore::PackEntry *e = shop.find("shopman.bin")) shopman_ = std::make_shared<dccore::MessageBank>(dccore::MessageBank::parse(shop.data(*e)));
    } catch (const std::exception &) {
    }
    try {   // the names of the Georama parts and tips
        dccore::Pack emenu(disc_->read("commenu/a_usa/emenu.pak"));
        if (const dccore::PackEntry *e = emenu.find("atrames.bin")) atla_text_ = std::make_shared<dccore::MessageBank>(dccore::MessageBank::parse(emenu.data(*e)));
    } catch (const std::exception &) {
    }
}

// Everything that turns a layout into a playable floor: the floor node, the player, the sword, the monsters (arranged with monster_rand_state) and, when given,
// the chests / statues / trigger areas and the player record to play with.
// Every identifier-like string of dun/script/d0N/event.stb: a superset of the object names its _GET_OBJHDL calls use.
std::set<std::string> DcModels::script_names(int map_no) {
    std::set<std::string> out;
    if (!disc_ || map_no < 0 || map_no > 6) return out;
    char spath[96];
    std::snprintf(spath, sizeof spath, "dun/script/d%02d/event.stb", map_no + 1);
    try {
        const dccore::Bytes stb = disc_->read(spath);
        std::string cur;
        auto flush = [&]() { if (cur.size() >= 3 && cur.size() <= 32) out.insert(cur); cur.clear(); };
        for (uint8_t ch : stb) {
            if (std::isalnum(ch) || ch == '_') cur.push_back(char(ch));
            else flush();
        }
        flush();
    } catch (const std::exception &) {
    }
    return out;
}

Node3D *DcModels::assemble_walker(std::shared_ptr<dccore::Dungeon> dungeon, const dccore::FloorLayout &layout, int map_no, int floor, const String &character_game_path, bool monsters,
        uint32_t monster_rand_state, const dccore::FloorEvents *events, const dccore::UserStatus *status, int gate_key) {
    load_tables();
    DcDungeon *floor_node = memnew(DcDungeon);
    const std::set<std::string> object_names = script_names(map_no);
    floor_node->set_dynamic_objects(object_names);
    floor_node->setup(dungeon, layout);
    DcCharacter *player = nullptr;
    std::shared_ptr<dccore::PlayerKit> kit;
    if (!character_game_path.is_empty()) {   // (the character in play is the one the record says; the path only says whether to show a player at all)
        const int ch = status ? status->character() : 0;
        if (dun_bin_.empty()) dun_bin_ = disc_->read_root("DUN.BIN");
        const int weapon_id = status ? int(status->weapon(ch).id()) : dccore::kDefWeapon[0] + 1;
        const int element = status ? int(status->weapon(ch).element_choice()) : 0;
        kit = std::make_shared<dccore::PlayerKit>(dccore::load_player_kit(*disc_, dun_bin_, ch, weapon_id, element));
        player = memnew(DcCharacter);
        Dictionary info;
        player->setup(kit->model, info);
    }
    DcWalker *walker = memnew(DcWalker);
    walker->setup(dungeon, layout, player, floor_node);
    if (status) walker->set_initial_status(*status);
    walker->set_atra_handler([this, map_no, floor](dccore::UserStatus &st, int idx) {   // getAtraToSaveData + GetAtraData: the part or tip goes to the town's Atla, the entry of the table is used up
        if (map_no < 0 || map_no > 5 || idx < 0 || idx >= 100 || floor < 0) return;
        const size_t row = 0x2078 + size_t(map_no) * 0x4b0 + size_t(idx) * 0xc;
        const int id = st.get<int32_t>(row);
        if (id >= 0 && geo_tables_) dccore::georama_atra_get(save_, *geo_tables_, map_no, id);
        const size_t list = 0x278 + size_t(map_no) * 0x500 + size_t(floor) * 0x20;
        bool marked = false;
        for (int pass = 0; pass < 2 && !marked; ++pass)
            for (int s = 0; s < 8; ++s) {
                const size_t at = list + size_t(s) * 4;
                if (st.get<int32_t>(at) == (pass == 0 ? idx : -2)) { st.set<int32_t>(at, -3); marked = true; break; }
            }
        if (marked) {
            const int left = st.get<int32_t>(row + 8) - 1;
            st.set<int32_t>(row + 8, left);
            if (left == 0) st.set<int32_t>(row, -1);
        }
    });
    if (monsters) {
        if (kit) {   // the weapon in hand: its model hangs on the character's "weapon" frame
            walker->set_kit(kit);
            walker->set_weapon_changer([this](int character, int weapon_id, int element) -> std::shared_ptr<dccore::PlayerKit> {
                try {
                    if (dun_bin_.empty()) dun_bin_ = disc_->read_root("DUN.BIN");
                    return std::make_shared<dccore::PlayerKit>(dccore::load_player_kit(*disc_, dun_bin_, character, weapon_id, element));
                } catch (const std::exception &) {
                    return nullptr;
                }
            });
            DcCharacter *wnode = memnew(DcCharacter);
            Dictionary winfo;
            wnode->setup(kit->weapon_model, winfo);
            wnode->set_playing(false);
            walker->set_weapon_node(wnode);
        }
        walker->set_item_db(item_db_);
        auto unit = std::make_shared<dccore::MonsterUnit>();
        if (!layout.fixed || boss_next_ || story_next_) unit->load_floor(*disc_, *tables_, map_no, floor, false);
        if (boss_next_) {
            walker->set_boss_floor(true);
            {   // where the boss room's own floor-start / intro script leaves the hero (read from dun/script/d0N/event.stb, checked by running the script: tests --event-end); the floor's 'in' frame is the wrong place for these
                struct Start { int map, floor; double x, z, yaw; };
                static const Start kStarts[] = {
                    {2, 17, 0.0, 150.0, -3.14},     // d03: function 100 (_SET_NPC_POS(-1, 0, 10, 150)); the 'in' frame lies behind a wall that the hero cannot cross
                    {3, 17, 0.0, -100.0, 0.0},      // d04: the intro (fn_26d04) ends with the hero at z = -100 (the arrival scene alone ends at z = 380; the live game agrees)
                    {5, 24, 0.0, 48.32, 3.06},      // d06 stage 1: function 199 (the floor's own start frame is the arena's centre, where the boss's shock wave lands)
                    {6, 99, 8.17, 148.41, -2.90},   // d07: the intro (function 199) ends close to the boss
                };
                for (const Start &st : kStarts)
                    if (st.map == map_no && st.floor == floor) walker->set_start(st.x, st.z, st.yaw);
            }
            walker->set_boss_handler([this](int m, int f) { boss_cleared(m, f); });
        }
        walker->set_coop_players(coop_players_);
        walker->set_monsters(unit, *tables_, map_no, monster_rand_state, dccore::parse_collision_mds(disc_->read("dun/etc/atrcol.mds")), events, floor, layout.map1_variant);
        if (events) walker->set_events(*events, dccore::floor_markers(*dungeon, layout), event_tables_, map_no, floor, gate_key);
        if (back_next_.present) {   // the back floor of this random floor (built right after the main one, see start_floor)
            BackPackage pkg = std::move(back_next_);
            back_next_ = BackPackage{};
            walker->set_back_floor(pkg.layout, pkg.events, dccore::floor_markers(*dungeon, pkg.layout),
                    [dungeon, object_names](const dccore::FloorLayout &l) -> DcDungeon * {
                        DcDungeon *node = memnew(DcDungeon);
                        node->set_dynamic_objects(object_names);
                        node->setup(dungeon, l);
                        node->set_back(true);
                        return node;
                    },
                    [this, map_no, floor](dccore::MonsterUnit &u, bool back) { u.load_floor(*disc_, *tables_, map_no, floor, back); });
        }
        const bool boss_script = boss_next_ && std::getenv("DC_BOSS_HARDCODED") == nullptr;   // (the boss room's own floor-start function runs: the intro scene, the hero's place, the monsters, the gate and trigger markers; DC_BOSS_HARDCODED=1 skips it for comparisons)
        if ((!boss_next_ || boss_script) && map_no >= 0 && map_no < 7) {   // the dungeon's own script runs the story rooms' events and the markers of the character rooms of random floors
            char spath[96];
            std::snprintf(spath, sizeof spath, "dun/script/d%02d/event.stb", map_no + 1);
            const dccore::Bytes stb = disc_->read(spath);
            std::shared_ptr<dccore::MessageBank> bank;   // the script's texts are the bank stored behind its code (header word 5)
            if (stb.size() > 0x18) {
                uint32_t off;
                std::memcpy(&off, stb.data() + 0x14, 4);
                if (off > 0x18 && off < stb.size()) bank = std::make_shared<dccore::MessageBank>(dccore::MessageBank::parse(dccore::View(stb.data() + off, stb.size() - off)));
            }
            walker->pack_loader = [this](const std::string &p) -> std::shared_ptr<dccore::Pack> {
                try { if (disc_->contains(p)) return std::make_shared<dccore::Pack>(disc_->read(p)); } catch (const std::exception &) {}
                return nullptr;
            };
            auto session = std::make_shared<dccore::DungeonSession>(stb, map_no, &save_, walker->status_ptr(), &walker->game_rand(), item_db_.get(), system_text_.get(), bank.get(), walker->event_host());
            walker->set_dungeon_session(session, (story_next_ || boss_script) ? floor : -1);
            walker->set_ruby_key(ruby_key_);
        }
    }
    last_info_ = floor_node->get_info();
    return walker;
}

Node3D *DcModels::build_walker(const String &name, int seed, int rooms, int map_no, const String &character_game_path, int floor, bool monsters, int64_t arrange_seed) {
    if (!disc_) { UtilityFunctions::push_error("DcModels: no ISO opened"); return nullptr; }
    try {
        load_tables();
        const std::string stem = name.utf8().get_data();
        dccore::Bytes cfg = disc_->read("dun/" + stem + ".cfg");
        dccore::Pack pack(disc_->read("dun/mpd_pack/" + stem + ".mpd"));
        auto dungeon = std::make_shared<dccore::Dungeon>(dccore::load_dungeon(std::string(cfg.begin(), cfg.end()), pack));
        dccore::GenState gen;
        gen.tables = tables_.get();
        gen.floor_no = floor;
        dccore::FloorLayout layout = dccore::build_random_map(uint32_t(seed), rooms, map_no, true, &gen);
        // a stand-alone floor (tests, screenshots) with a fresh player record: chests and statues as the game would put them
        dccore::UserStatus status = dccore::UserStatus::new_game();
        dccore::GameRand rng;
        rng.seed(layout.rand_state);
        if (status.get<int8_t>(0x42bf + size_t(map_no)) == -1 && map_no < 6) dccore::atra_list_make(status, *event_tables_, map_no, rng);
        rng.seed(layout.rand_state);
        dccore::FloorEvents events = dccore::make_floor_events(layout, map_no);
        dccore::build_event_data(events, layout, *tables_, *event_tables_, status, map_no, floor, true, rng);
        return assemble_walker(dungeon, layout, map_no, floor, character_game_path, monsters, arrange_seed >= 0 ? uint32_t(arrange_seed) : rng.state, &events, &status,
                dccore::gate_key_for(map_no, layout.map1_variant));
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_walker: ") + e.what());
        return nullptr;
    }
}

Node *DcModels::build_sound() {
    if (!disc_) return nullptr;
    try {
        DcSound *node = memnew(DcSound);
        node->setup(std::make_unique<dccore::snd::SoundSystem>(*disc_));
        return node;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_sound: ") + e.what());
        return nullptr;
    }
}

Node3D *DcModels::make_town_walk(std::shared_ptr<dccore::Town> town, int map_index, int start_event, int old_map) {
    DcTown *scene = memnew(DcTown);
    scene->setup(town);
    // the hero of the town: Toan's village model (gedit/<town>/chara/c01d.chr)
    DcCharacter *player = memnew(DcCharacter);
    Dictionary info;
    const std::string town_name = town->interior ? town->parent : town->name;
    bool made = false;
    const std::string hero_path = "gedit/" + town_name + "/chara/c01d.chr";
    if (disc_->contains(hero_path)) {
        try {
            dccore::Pack pk(disc_->read(hero_path));
            player->setup(std::make_shared<dccore::Character>(dccore::load_character(pk, "info.cfg")), info);
            made = true;
        } catch (const std::exception &) {
        }
    }
    if (!made) {
        if (dun_bin_.empty()) dun_bin_ = disc_->read_root("DUN.BIN");
        const int ch = status_.character();
        auto kit = std::make_shared<dccore::PlayerKit>(dccore::load_player_kit(*disc_, dun_bin_, ch, int(status_.weapon(ch).id()), 0));
        player->setup(kit->model, info);
    }
    TownEnv env;
    env.disc = disc_.get();
    env.save = &save_;
    env.status = &status_;
    env.items = item_db_.get();
    env.rng = &rng_;
    env.system_text = system_text_;
    env.atla_text = atla_text_;
    env.geo_tables = geo_tables_.get();
    env.fish_tables = fish_tables_.get();
    env.old_map = old_map;
    env.map_index = map_index;
    DcTownWalk *walk = memnew(DcTownWalk);
    walk->setup(scene, player, env, start_event);
    last_info_ = scene->get_info();
    return walk;
}

static int town_map_index(const std::string &stem) {
    return stem.size() == 3 && stem[0] == 'e' ? std::atoi(stem.c_str() + 1) - 1 : 10 + std::atoi(stem.c_str() + 1);
}

Node3D *DcModels::build_town(const String &name, bool complete, int start_event, int old_map) {
    if (!disc_) { UtilityFunctions::push_error("DcModels: no ISO opened"); return nullptr; }
    try {
        load_tables();
        const std::string stem = name.utf8().get_data();
        std::vector<dccore::PlacedPart> layout;
        const int town_no = town_map_index(stem);
        if (complete && disc_->contains("gedit/" + stem + "/gdata0.edt")) layout = dccore::parse_gdata_edt(disc_->read("gedit/" + stem + "/gdata0.edt"));
        else if (!complete && town_no >= 0 && town_no <= 5 && town_override_.count(town_no)) {   // co-op: the host's town
            dccore::SaveData tmp;
            const auto &blk = town_override_[town_no];
            std::memcpy(tmp.raw.data() + dccore::SaveData::kGrd + size_t(town_no) * dccore::SaveData::kGrdSize, blk.data(), std::min(blk.size(), dccore::SaveData::kGrdSize));
            layout = dccore::Georama::saved_layout(tmp, town_no);
        } else if (!complete && town_no >= 0 && town_no <= 5) layout = dccore::Georama::saved_layout(save_, town_no);   // what the player built so far
        auto town = std::make_shared<dccore::Town>(dccore::load_town(*disc_, stem, layout));
        if (complete && town_no >= 0 && town_no <= 4) grant_atla(town_no, true);   // the developer's layout is a finished town
        return make_town_walk(town, town_no, start_event, old_map);
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_town: ") + e.what());
        return nullptr;
    }
}

// Every part and tip of a town's Atla (what its dungeon holds); with `install` each tip is also set in its house and the completion events count as played.
void DcModels::grant_atla(int town, bool install) {
    if (!disc_ || town < 0 || town > 4) return;
    load_tables();
    for (int p = 0; p < 24; ++p) {
        if (geo_tables_->parts[town][p].max <= 0) break;
        dccore::georama_atra_get(save_, *geo_tables_, town, p);
        for (int k = 0; k < 6; ++k) {
            const int chip = geo_tables_->parts[town][p].elem[k].id;
            if (chip < 0) continue;
            if (install) {
                uint8_t *rec = save_.edit_parts_info(town, p);
                if (rec) { const int16_t one = 1; std::memcpy(rec + 10 + 2 * k, &one, 2); }
            } else {
                dccore::georama_atra_get(save_, *geo_tables_, town, 0x28 + chip);
            }
        }
        if (install)
            if (uint8_t *rec = save_.edit_parts_info(town, p)) { uint32_t f; std::memcpy(&f, rec, 4); f |= 1u; std::memcpy(rec, &f, 4); }
    }
}

Node3D *DcModels::build_interior(const String &town_name, const String &house, int start_event, int variant) {
    if (!disc_) { UtilityFunctions::push_error("DcModels: no ISO opened"); return nullptr; }
    try {
        load_tables();
        const std::string stem = town_name.utf8().get_data();
        auto town = std::make_shared<dccore::Town>(dccore::load_interior(*disc_, stem, house.utf8().get_data(), variant));
        return make_town_walk(town, town_map_index(stem), start_event, 0);
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_interior: ") + e.what());
        return nullptr;
    }
}

void DcModels::new_game(int64_t seed) {
    save_.initialize();
    status_ = dccore::UserStatus::new_game();
    rng_.seed(uint32_t(seed));
}

Node3D *DcModels::start_floor(const String &name, int map_no, int floor, int rooms, const String &character_game_path) {
    if (!disc_) { UtilityFunctions::push_error("DcModels: no ISO opened"); return nullptr; }
    try {
        load_tables();
        const std::string stem = name.utf8().get_data();
        dccore::Bytes cfg = disc_->read("dun/" + stem + ".cfg");
        dccore::Pack pack(disc_->read("dun/mpd_pack/" + stem + ".mpd"));
        auto dungeon = std::make_shared<dccore::Dungeon>(dccore::load_dungeon(std::string(cfg.begin(), cfg.end()), pack));
        if (coop_seed_ >= 0) ruby_key_ = int((coop_seed_ >> 4) % 5);   // (co-op: every player's crystals have the same colour)
        else if (stem != ruby_cfg_) {   // TEIGIAnalyz draws the colour when the cfg is read anew
            ruby_cfg_ = stem;
            ruby_key_ = std::clamp(int(5.0 * double(rng_.next()) / 2147483648.0), 0, 4);
        }
        if (map_no == 2 && floor == 8) ruby_key_ = 0;
        status_.set<int8_t>(0, int8_t(map_no));
        status_.set<int8_t>(2, int8_t(floor));
        status_.set<int32_t>(0x8b10, int(get_floor_map(map_no, floor).get("zone", -1)));   // the limit zone the floor-start script sets (a forced character, weapons wearing, thirst)
        {   // a forced-character floor (zone 0..5): the whole party is replaced by that character (_SET_MAIN_CHR2 = selectChrUnit)
            const int zone = status_.get<int32_t>(0x8b10);
            if (zone >= 0 && zone <= 5 && status_.get<int8_t>(4) != zone) {
                if (status_.get<int16_t>(0x450c + size_t(zone) * 0xaa8) < 0x101) join_party(zone);
                status_.set<int8_t>(4, int8_t(zone));
            }
        }
        if (map_no >= 0 && map_no < 7 && deepest_[map_no] < floor) deepest_[map_no] = int16_t(floor);
        std::fill(status_.raw.begin() + 0x8aa4, status_.raw.begin() + 0x8aa4 + 50, uint8_t(0));   // ClearEventFlag: the per-floor event flags (a back-floor entrance's unlock ...)
        if (map_no < 6 && status_.get<int8_t>(0x42bf + size_t(map_no)) == -1) {   // LoadData: the first time in a dungeon
            dccore::GameRand own;   // (co-op: the player's own Atla list must not move the shared generator)
            own.seed(uint32_t(coop_seed_ >= 0 ? coop_seed_ ^ 0x5a5a5a5a : rng_.state));
            dccore::atra_list_make(status_, *event_tables_, map_no, coop_seed_ >= 0 ? own : rng_);
        }
        if (get_floor_map(map_no, floor).get("kind", 1) != Variant(1)) {   // a story or boss room: a fixed map, no random floor, no chests
            dccore::FloorLayout fixed = dccore::make_fixed_layout(map_no);
            dccore::FloorEvents none = dccore::make_floor_events(fixed, map_no);
            boss_next_ = get_floor_map(map_no, floor).get("kind", 1) == Variant(2);
            story_next_ = get_floor_map(map_no, floor).get("kind", 1) == Variant(0);
            Node3D *node = assemble_walker(dungeon, fixed, map_no, floor, character_game_path, true, rng_.state, &none, &status_, 0);
            boss_next_ = false;
            story_next_ = false;
            return node;
        }
        if (coop_seed_ >= 0) rng_.seed(uint32_t(coop_seed_));   // co-op: the host's seed makes every player's floor the same
        // buildRandomMap draws the floor's seed from the running generator and reseeds it with it
        const uint32_t seed = dccore::derive_floor_seed(rng_.state);
        rng_.next();
        dccore::GenState gen;
        gen.tables = tables_.get();
        gen.floor_no = floor;
        gen.party_size = status_.get<int8_t>(5);
        gen.chara_state = status_.get<int32_t>(0x8b10);
        dccore::FloorLayout layout = dccore::build_random_map(seed, rooms, map_no, true, &gen);
        rng_.seed(layout.rand_state);
        dccore::FloorEvents events = dccore::make_floor_events(layout, map_no);
        dccore::build_event_data(events, layout, *tables_, *event_tables_, coop_host_status_ ? *coop_host_status_ : status_, map_no, floor, true, rng_);
        {   // BtCleatRandomMap goes on with the back floor: the same part list, 6 rooms again, the generator's stream simply continues (no new seed), its own chests
            dccore::FloorLayout ura = dccore::build_random_map(rng_.state, rooms, map_no, false, &gen);
            rng_.seed(ura.rand_state);
            dccore::FloorEvents ura_events = dccore::make_floor_events(ura, map_no);
            dccore::build_event_data(ura_events, ura, *tables_, *event_tables_, coop_host_status_ ? *coop_host_status_ : status_, map_no, floor, false, rng_);
            back_next_.present = true;
            back_next_.layout = std::move(ura);
            back_next_.events = std::move(ura_events);
        }
        return assemble_walker(dungeon, layout, map_no, floor, character_game_path, true, rng_.state, &events, &status_, dccore::gate_key_for(map_no, layout.map1_variant));
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.start_floor: ") + e.what());
        return nullptr;
    }
}

void DcModels::end_floor(Node3D *walker_node) {
    DcWalker *w = Object::cast_to<DcWalker>(walker_node);
    if (!w || !w->status_ptr()) return;
    status_ = *w->status_ptr();
    rng_ = w->game_rand();
}

Dictionary DcModels::get_session_status() const {
    Dictionary d;
    const int ch = status_.character();
    d["character"] = ch;
    d["dungeon"] = int(status_.get<int8_t>(0));
    d["floor"] = int(status_.get<int8_t>(2));
    d["life"] = int(status_.life(ch));
    d["max_life"] = int(status_.max_life(ch));
    d["gold"] = int(status_.get<uint16_t>(0x4346));
    return d;
}

Dictionary DcModels::get_floor_map(int dungeon, int floor) {
    Dictionary d;
    if (!disc_) return d;
    char path[64];
    std::snprintf(path, sizeof path, "dun/script/d%02d/event.stb", dungeon + 1);
    try {
        if (dungeon == 5 && floor >= 24 && save_.game_int(10) == 0) save_.set_game_int(10, 1);   // (the scene s16 that starts the stage is skipped)
        const dccore::FloorMapChoice c = dccore::choose_floor_map(disc_->read(path), floor, [this](int i) { return int(save_.game_int(i)); }, [this](int i) { return save_.game_flag(i); });
        d["name"] = String(c.name.c_str());
        d["kind"] = c.kind;
        d["zone"] = c.zone;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.get_floor_map: ") + e.what());
    }
    return d;
}

void DcModels::join_party(int character) {
    load_tables();
    dccore::join_party(status_, *item_db_, character, rng_);
}

Array DcModels::get_party() const {
    static const char *kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};
    Array out;
    for (int ch = 0; ch < 6; ++ch) {
        Dictionary d;
        d["character"] = ch;
        d["name"] = String(kNames[ch]);
        d["joined"] = status_.get<int16_t>(0x450c + size_t(ch) * 0xaa8) >= 0x101;
        d["life"] = int(status_.life(ch));
        d["max_life"] = int(status_.max_life(ch));
        d["weapon_id"] = int(status_.weapon(ch).id());
        out.push_back(d);
    }
    return out;
}

bool DcModels::switch_character(Node3D *walker_node, int character) {
    DcWalker *w = Object::cast_to<DcWalker>(walker_node);
    if (!w || !w->status_ptr() || character < 0 || character > 5 || !disc_) return false;
    dccore::UserStatus *st = w->status_ptr();
    if (st->get<int16_t>(0x450c + size_t(character) * 0xaa8) < 0x101) return false;
    { const int zone = st->get<int32_t>(0x8b10); if (zone >= 0 && zone <= 5 && zone != character) return false; }   // (the quick-change ring is refused on a forced-character floor)
    if (st->life(character) < 1) return false;
    if (st->character() == character) return true;
    try {
        if (dun_bin_.empty()) dun_bin_ = disc_->read_root("DUN.BIN");
        const int weapon_id = int(st->weapon(character).id());
        const int element = int(st->weapon(character).element_choice());
        auto kit = std::make_shared<dccore::PlayerKit>(dccore::load_player_kit(*disc_, dun_bin_, character, weapon_id, element));
        DcCharacter *player = memnew(DcCharacter);
        Dictionary info;
        player->setup(kit->model, info);
        DcCharacter *wnode = memnew(DcCharacter);
        Dictionary winfo;
        wnode->setup(kit->weapon_model, winfo);
        wnode->set_playing(false);
        return w->swap_character(kit, player, wnode);
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.switch_character: ") + e.what());
        return false;
    }
}

void DcModels::set_character(int character) {
    if (character < 0 || character > 5) return;
    if (status_.get<int16_t>(0x450c + size_t(character) * 0xaa8) < 0x101) join_party(character);
    status_.set<int8_t>(4, int8_t(character));
}

bool DcModels::cheat_give_item(int item) {
    load_tables();
    if (dccore::check_item_get(status_, item) != 0) return false;
    dccore::get_item(status_, *item_db_, item, rng_);
    return true;
}

void DcModels::game_over(Node3D *walker_node) {
    end_floor(walker_node);
    // CDngStatusData::SetDead: half of the gold is lost; everybody is back on their feet
    status_.set<uint16_t>(0x4346, uint16_t(status_.get<uint16_t>(0x4346) >> 1));
    for (int k = 0; k < 6; ++k) status_.set_life(k, status_.max_life(k));
}

// commenu/itemicon.img: one 256x768 sheet, 8 x 24 icons of 32x32; icon n belongs to item id 65 + n (ids without an artwork show their number).
// commenu/a_usa/wepicon.img: the weapons, icon n belongs to weapon id 257 + n.
Ref<ImageTexture> DcModels::get_item_icon(int item) {
    const bool weapon = item >= 257;
    if (!disc_ || item < 0x51) return Ref<ImageTexture>();
    load_tables();
    auto it = item_icons_.find(item);
    if (it != item_icons_.end()) return it->second;
    std::unique_ptr<dccore::Image> &sheet_ptr = weapon ? weapon_sheet_ : item_sheet_;
    if (!sheet_ptr) {
        try {
            const dccore::Bytes img = disc_->read("commenu/a_usa/itemlst.img");   // the icon sheets of the menus: "itemicon" and "wepicon"
            for (const dccore::ImgTexture &t : dccore::img_textures(img))
                if (t.name == (weapon ? "wepicon" : "itemicon")) {
                    sheet_ptr = std::make_unique<dccore::Image>(dccore::Tim2(img, t.offset).decode(false, t.swizzled));
                    break;
                }
        } catch (const std::exception &) {
        }
        if (!sheet_ptr) sheet_ptr = std::make_unique<dccore::Image>();
    }
    Ref<ImageTexture> tex;
    const dccore::Image &sheet = *sheet_ptr;
    const int n = item_db_ ? item_db_->icon_cell(item) : -1, col = n & 7, row = n >> 3;
    if (n < 0) { item_icons_[item] = tex; return tex; }
    if (sheet.width >= 256 && int(sheet.height) >= (row + 1) * 32) {
        PackedByteArray data;
        data.resize(32 * 32 * 4);
        for (int y = 0; y < 32; ++y)
            std::memcpy(data.ptrw() + y * 128, sheet.rgba.data() + (size_t(row) * 32 + size_t(y)) * sheet.width * 4 + size_t(col) * 128, 128);
        tex = ImageTexture::create_from_image(Image::create_from_data(32, 32, false, Image::FORMAT_RGBA8, data));
    }
    item_icons_[item] = tex;
    return tex;
}

Ref<ImageTexture> DcModels::get_pack_texture(const String &pack_path, const String &texture_name) {
    if (!disc_) return Ref<ImageTexture>();
    const std::string path = pack_path.utf8().get_data(), name = texture_name.utf8().get_data();
    const std::string key = path + "#" + name;
    auto it = pack_textures_.find(key);
    if (it != pack_textures_.end()) return it->second;
    Ref<ImageTexture> tex;
    try {
        dccore::Bytes raw = disc_->read(path);
        // a pack of .img containers, or a single .img container
        std::vector<std::pair<const uint8_t *, size_t>> containers;
        std::unique_ptr<dccore::Pack> pk;
        dccore::View whole(raw);
        if (raw.size() > 4 && (std::memcmp(raw.data(), "IMG", 3) == 0 || std::memcmp(raw.data(), "IM2", 3) == 0)) {
            containers.push_back({raw.data(), raw.size()});
        } else {
            pk = std::make_unique<dccore::Pack>(raw);
            for (const dccore::PackEntry &e : pk->entries())
                if (e.name.size() > 4 && (e.name.compare(e.name.size() - 4, 4, ".img") == 0 || e.name.compare(e.name.size() - 4, 4, ".IMG") == 0)) {
                    dccore::View v = pk->data(e);
                    containers.push_back({v.data(), v.size()});
                }
        }
        for (auto &c : containers) {
            dccore::View v(c.first, c.second);
            for (const dccore::ImgTexture &t : dccore::img_textures(v))
                if (name.empty() || t.name == name) {
                    const dccore::Image im = dccore::Tim2(v, t.offset).decode(false, t.swizzled);
                    PackedByteArray bytes;
                    bytes.resize(int64_t(im.rgba.size()));
                    std::memcpy(bytes.ptrw(), im.rgba.data(), im.rgba.size());
                    tex = ImageTexture::create_from_image(Image::create_from_data(int(im.width), int(im.height), false, Image::FORMAT_RGBA8, bytes));
                    break;
                }
            if (tex.is_valid()) break;
        }
    } catch (const std::exception &) {
    }
    pack_textures_[key] = tex;
    return tex;
}

String DcModels::get_item_name(int item) {
    if (!disc_) return String();
    load_tables();
    if (item_db_) return String::utf8(system_text_->get(item_db_->name_message(item), {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"}).c_str());
    return String::utf8(dccore::item_text_id_name(*system_text_, item).c_str());
}

String DcModels::get_system_text(int id) {
    if (!disc_) return String();
    load_tables();
    return String::utf8(system_text_->get(id, {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"}).c_str());
}

// Save file: "DCSV", version 3: generator state (u32), deepest floor per dungeon (7 x int16, padded to 16 bytes), then the game's whole CSaveData (0x131c0 bytes in
// the game's own layout, the player record inside it). Versions 1 and 2 hold only the player record (0x9000 bytes) and are still read.
bool DcModels::save_game(const String &path) {
    std::ofstream f(std::filesystem::path(path.utf8().get_data()), std::ios::binary);
    if (!f) return false;
    save_.store_status(status_);
    const uint32_t header[3] = {0x56534344u, 3u, rng_.state};
    f.write(reinterpret_cast<const char *>(header), sizeof header);
    int16_t deepest[8] = {};
    for (int i = 0; i < 7; ++i) deepest[i] = deepest_[i];
    f.write(reinterpret_cast<const char *>(deepest), sizeof deepest);
    f.write(reinterpret_cast<const char *>(save_.raw.data()), std::streamsize(save_.raw.size()));
    return bool(f);
}

bool DcModels::load_game(const String &path) {
    std::ifstream f(std::filesystem::path(path.utf8().get_data()), std::ios::binary);
    if (!f) return false;
    uint32_t header[3] = {};
    f.read(reinterpret_cast<char *>(header), sizeof header);
    if (!f || header[0] != 0x56534344u || header[1] < 1u || header[1] > 3u) return false;
    for (int16_t &d : deepest_) d = -1;
    if (header[1] == 3u) {
        int16_t deepest[8] = {};
        f.read(reinterpret_cast<char *>(deepest), sizeof deepest);
        dccore::SaveData sd;
        f.read(reinterpret_cast<char *>(sd.raw.data()), std::streamsize(sd.raw.size()));
        if (!f) return false;
        save_ = sd;
        save_.load_status(status_);
        for (int i = 0; i < 7; ++i) deepest_[i] = deepest[i];
    } else {
        dccore::UserStatus s;
        f.read(reinterpret_cast<char *>(s.raw.data()), std::streamsize(s.raw.size()));
        if (!f) return false;
        status_ = s;
        if (header[1] >= 2u) f.read(reinterpret_cast<char *>(deepest_), sizeof deepest_);
    }
    rng_.seed(header[2]);
    return true;
}

Array DcModels::get_bag() const {
    Array out;
    for (int i = 0; i < dccore::kBagSlots; ++i) {
        const int id = status_.get<int16_t>(0x436e + size_t(i) * 2);
        if (id < 0x84 || id > 0x100) continue;
        Dictionary d;
        d["slot"] = i;
        d["id"] = id;
        d["value"] = int(status_.get<int16_t>(0x443c + size_t(i) * 2));
        out.push_back(d);
    }
    return out;
}

Dictionary DcModels::use_bag_item(int slot) {
    Dictionary d;
    d["used"] = false;
    if (!disc_ || slot < 0 || slot >= dccore::kBagSlots) return d;
    load_tables();
    const int id = status_.get<int16_t>(0x436e + size_t(slot) * 2);
    if (id < 0x84 || id > 0x100) return d;
    const dccore::UseEffect e = dccore::use_item_effect(status_, *item_db_, id);
    if (!e.used) return d;
    dccore::consume_bag_slot(status_, slot);
    d["used"] = true;
    d["life"] = e.life;
    d["water"] = e.water;
    return d;
}

Array DcModels::get_weapons() const {
    Array out;
    const int ch = status_.character();
    for (int i = 0; i < 11; ++i) {
        const size_t at = 0x450c + size_t(ch) * 0xaa8 + size_t(i) * 0xf8;
        if (status_.get<int16_t>(at) < 0x101) continue;
        const dccore::WeaponView w{status_.raw.data() + at};
        Dictionary d;
        d["slot"] = i;
        d["id"] = int(w.id());
        d["level"] = int(w.level());
        d["attack"] = int(w.attack());
        d["durability"] = w.durability();
        d["max_durability"] = int(w.max_durability());
        d["exp"] = int(w.exp());
        d["current"] = status_.current_weapon_slot(ch) == i;
        out.push_back(d);
    }
    return out;
}

bool DcModels::equip_weapon(int slot) {
    if (slot < 0 || slot >= 11) return false;
    const int ch = status_.character();
    if (status_.get<int16_t>(0x450c + size_t(ch) * 0xaa8 + size_t(slot) * 0xf8) < 0x101) return false;
    status_.set<int8_t>(0x4340 + size_t(ch), int8_t(slot));
    return true;
}

void DcModels::rest() {
    for (int k = 0; k < 6; ++k) {
        status_.set_life(k, status_.max_life(k));
        status_.set<int16_t>(0x8a68 + size_t(k) * 2, 0);
        status_.set<float>(0x4304 + size_t(k) * 4, status_.get<float>(0x42ec + size_t(k) * 4));
        status_.set<int16_t>(0x8ae2 + size_t(k) * 2, 0);
    }
}

Dictionary DcModels::get_village_status() const {
    Dictionary d;
    const int ch = status_.character();
    d["life"] = int(status_.life(ch));
    d["max_life"] = int(status_.max_life(ch));
    d["water"] = status_.get<float>(0x4304 + size_t(ch) * 4);
    d["max_water"] = status_.get<float>(0x42ec + size_t(ch) * 4);
    d["gold"] = int(status_.get<uint16_t>(0x4346));
    d["character"] = ch;
    d["weapon_id"] = int(status_.weapon(ch).id());
    return d;
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// shops

static const std::vector<std::string> kHeroNames = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

String DcModels::shop_text(int id) {
    load_tables();
    if (shopman_) { const std::string t = shopman_->get(id, kHeroNames); if (!t.empty()) return String::utf8(t.c_str()); }
    if (shop_desc_) return String::utf8(shop_desc_->get(id, kHeroNames).c_str());
    return String();
}

String DcModels::item_description(int id) {
    load_tables();
    if (!item_db_ || !shop_desc_) return String();
    return String::utf8(shop_desc_->get(item_db_->name_message(id) + 400, kHeroNames).c_str());   // ComItemInfo short + 500 (the name message is that + 100)
}

Array DcModels::shop_stock(int shop) {
    Array out;
    if (!disc_) return out;
    load_tables();
    for (int id : shop_tables_->stock(shop)) {
        Dictionary d;
        d["id"] = id;
        d["name"] = get_item_name(id);
        d["price"] = shop_tables_->buy_price(id);
        d["kind"] = id > 0x100 ? 1 : (id < 0x84 ? 2 : 0);
        d["desc"] = item_description(id);
        out.push_back(d);
    }
    return out;
}

int DcModels::shop_buy(int shop, int index) {
    load_tables();
    return int(dccore::shop_buy(status_, *item_db_, *shop_tables_, shop, index, rng_));
}

Dictionary DcModels::item_entry(dccore::ItemRef ref, bool for_sale) const {
    Dictionary d;
    const int id = dccore::item_ref_id(status_, ref);
    d["kind"] = ref.kind;
    d["owner"] = ref.owner;
    d["slot"] = ref.slot;
    d["id"] = id;
    d["value"] = ref.kind == 0 ? int(status_.get<int16_t>(0x443c + size_t(ref.slot) * 2)) : 0;
    dccore::ShopResult why = dccore::ShopResult::Ok;
    d["price"] = for_sale ? dccore::sale_price(status_, *item_db_, *shop_tables_, ref, &why) : 0;
    d["why"] = int(why);
    d["equipped"] = ref.kind == 1 && status_.current_weapon_slot(ref.owner) == ref.slot;
    return d;
}

Array DcModels::shop_inventory(bool for_sale) {
    Array out;
    if (!disc_) return out;
    load_tables();
    for (int i = 0; i < dccore::kBagSlots; ++i) {
        dccore::ItemRef r{0, 0, i};
        if (dccore::item_ref_id(status_, r) < 0x84) continue;
        Dictionary d = item_entry(r, for_sale);
        d["name"] = get_item_name(int(d["id"]));
        out.push_back(d);
    }
    const int party = std::max(1, int(status_.get<int8_t>(5)));
    for (int ch = 0; ch < party && ch < 6; ++ch)
        for (int i = 0; i < 10; ++i) {
            dccore::ItemRef r{1, ch, i};
            if (dccore::item_ref_id(status_, r) <= 0x100) continue;
            Dictionary d = item_entry(r, for_sale);
            d["name"] = get_item_name(int(d["id"]));
            out.push_back(d);
        }
    for (int i = 0; i < 0x2b; ++i) {
        dccore::ItemRef r{2, 0, i};
        const int id = dccore::item_ref_id(status_, r);
        if (id <= 0x50 || id >= 0x84) continue;
        Dictionary d = item_entry(r, for_sale);
        d["name"] = get_item_name(id);
        out.push_back(d);
    }
    return out;
}

int DcModels::shop_sell(int kind, int owner, int slot) {
    load_tables();
    return int(dccore::shop_sell(status_, *item_db_, *shop_tables_, dccore::ItemRef{kind, owner, slot}));
}

Array DcModels::storage_list(int kind) {
    Array out;
    load_tables();
    const int n = kind == 0 ? dccore::kStorageItems : kind == 1 ? dccore::kStorageWeapons : dccore::kStorageAttach;
    const int lo = kind == 0 ? 0x84 : kind == 1 ? 0x101 : 0x51;
    for (int i = 0; i < n; ++i) {
        const int id = dccore::storage_id(save_, kind, i);
        if (id < lo) continue;
        Dictionary d;
        d["slot"] = i;
        d["id"] = id;
        d["name"] = get_item_name(id);
        d["value"] = dccore::storage_value(save_, kind, i);
        out.push_back(d);
    }
    return out;
}

int DcModels::storage_store(int kind, int owner, int slot) {
    load_tables();
    return int(dccore::storage_store(status_, save_, *item_db_, dccore::ItemRef{kind, owner, slot}));
}

int DcModels::storage_take(int kind, int slot) {
    load_tables();
    return int(dccore::storage_take(status_, save_, *item_db_, kind, slot, rng_));
}

Dictionary DcModels::storage_capacity(int kind) {
    Dictionary d;
    d["used"] = dccore::storage_count(save_, kind);
    d["max"] = kind == 0 ? dccore::kStorageItems : kind == 1 ? dccore::kStorageWeapons : dccore::kStorageAttach;
    return d;
}


// the option rows: where each lives (config ints of the save data; row 1 is the saved cursor short, row 7 the map density in the player record)
static int option_offset(int row) {
    static const int off[13] = {0, 0x650, 0x1c, 0x10, 0x14, 0x08, 0x0c, 0, 0x28, 0x24, 0x2c, 0x20, 0x18};
    return row >= 1 && row <= 12 ? off[row] : -1;
}

int DcModels::get_option(int row) const {
    if (row == 7) return int(status_.get<int32_t>(0x433c));
    if (row == 1) return int(save_.get<int16_t>(0x650));
    const int off = option_offset(row);
    return off >= 0 ? int(save_.get<int32_t>(size_t(off))) : 0;
}

void DcModels::set_option(int row, int value) {
    if (row == 7) { status_.set<int32_t>(0x433c, value); save_.set<int32_t>(0x3c, value); return; }
    if (row == 1) { save_.set<int16_t>(0x650, int16_t(value)); save_.set<int32_t>(0x40, value); return; }
    const int off = option_offset(row);
    if (off >= 0) save_.set<int32_t>(size_t(off), value);
}


// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// the weapon page

dccore::UserStatus *DcModels::menu_status(Object *walker) {
    if (walker) if (DcWalker *w = Object::cast_to<DcWalker>(walker)) if (dccore::UserStatus *st = w->status_ptr()) return st;
    return &status_;
}

String DcModels::menu_text(int id) {
    load_tables();
    return menu_text_ ? String::utf8(menu_text_->get(id, kHeroNames).c_str()) : String();
}

Array DcModels::wm_weapons(int ch, Object *walker) {
    Array out;
    load_tables();
    dccore::UserStatus &st = *menu_status(walker);
    if (ch < 0 || ch > 5) return out;
    for (int i = 0; i < dccore::kWeaponSlots; ++i) {
        const size_t at = 0x450c + size_t(ch) * 0xaa8 + size_t(i) * 0xf8;
        if (st.get<int16_t>(at) < 0x101) continue;
        const uint8_t *stored = st.raw.data() + at;
        uint8_t derived[0xf8];
        dccore::weapon_all_value_set(*item_db_, stored, derived, false);
        auto rd16 = [](const uint8_t *p, size_t o) { int16_t v; std::memcpy(&v, p + o, 2); return int(v); };
        Dictionary d;
        d["slot"] = i;
        d["id"] = rd16(stored, 0);
        d["name"] = get_item_name(rd16(stored, 0));
        d["level"] = rd16(stored, 2);
        d["attack"] = rd16(derived, 4);
        d["endurance"] = rd16(derived, 6);
        d["speed"] = rd16(derived, 8);
        d["magic"] = rd16(derived, 10);
        d["base_attack"] = rd16(stored, 4);
        d["base_endurance"] = rd16(stored, 6);
        d["base_speed"] = rd16(stored, 8);
        d["base_magic"] = rd16(stored, 10);
        d["max_durability"] = rd16(stored, 0xc);
        float dur;
        std::memcpy(&dur, stored + 0x10, 4);
        d["durability"] = dur;
        d["exp"] = rd16(stored, 0x14);
        d["max_exp"] = item_db_->weapon_max_exp(stored);
        d["element"] = int(stored[0x16]);
        Array el, sl;
        for (int e = 0; e < 5; ++e) el.push_back(int(int8_t(derived[0x17 + e])));
        for (int e = 0; e < 10; ++e) sl.push_back(int(int8_t(derived[0x1c + e])));
        d["elements"] = el;
        d["slayers"] = sl;
        d["flags"] = rd16(derived, 0xee);
        d["equipped"] = st.current_weapon_slot(ch) == i;
        d["default"] = dccore::is_default_weapon(rd16(stored, 0));
        Array holes;
        const uint8_t *row = item_db_->weapon(rd16(stored, 0));
        for (int k = 0; k < 6; ++k) {
            const int type = row ? int(row[0xb + k]) : 0;
            if (type <= 0) continue;
            Dictionary h;
            h["hole"] = k;
            h["type"] = type;
            const uint8_t *at_rec = stored + 0x28 + size_t(k) * 0x20;
            const int aid = rd16(at_rec, 0);
            h["id"] = (aid > 0x50 && aid < 0x84) ? aid : -1;
            h["name"] = (aid > 0x50 && aid < 0x84) ? get_item_name(aid) : String();
            h["double"] = stored[0xe8 + k] == 3;
            holes.push_back(h);
        }
        d["holes"] = holes;
        out.push_back(d);
    }
    return out;
}

Array DcModels::wm_attach_items(Object *walker) {
    Array out;
    load_tables();
    dccore::UserStatus &st = *menu_status(walker);
    for (int i = 0; i < 0x2b; ++i) {
        const uint8_t *p = st.raw.data() + 0x84fc + size_t(i) * 0x20;
        int16_t id;
        std::memcpy(&id, p, 2);
        if (id < 0x51 || id > 0x83) continue;
        Dictionary d;
        d["slot"] = i;
        d["id"] = int(id);
        d["name"] = get_item_name(id);
        Array stats;
        for (int k = 0; k < 4; ++k) { int16_t v; std::memcpy(&v, p + 8 + k * 2, 2); stats.push_back(int(v)); }
        for (int k = 0; k < 5; ++k) stats.push_back(int(int8_t(p[0x10 + k])));
        for (int k = 0; k < 10; ++k) stats.push_back(int(int8_t(p[0x15 + k])));
        d["stats"] = stats;
        if (id == 0x5a) { int16_t wid; std::memcpy(&wid, p + 2, 2); d["weapon"] = int(wid); }
        out.push_back(d);
    }
    return out;
}

int DcModels::wm_equip(int ch, int slot, Object *walker) { load_tables(); return int(dccore::weapon_equip(*menu_status(walker), ch, slot)); }
int DcModels::wm_discard(int ch, int slot, Object *walker) { load_tables(); return int(dccore::weapon_discard(*menu_status(walker), *item_db_, ch, slot, save_.game_flag(0x30))); }
int DcModels::wm_element(int ch, int slot, int element, Object *walker) { load_tables(); return int(dccore::weapon_set_element(*menu_status(walker), *item_db_, ch, slot, element)); }
int DcModels::wm_attach(int ch, int slot, int hole, int attach_slot, Object *walker) { load_tables(); return int(dccore::weapon_attach(*menu_status(walker), *item_db_, ch, slot, hole, attach_slot)); }
int DcModels::wm_detach(int ch, int slot, int hole, Object *walker) { load_tables(); return int(dccore::weapon_detach(*menu_status(walker), *item_db_, ch, slot, hole)); }
int DcModels::wm_repair(int ch, int slot, Object *walker) { load_tables(); return int(dccore::weapon_repair(*menu_status(walker), *item_db_, rng_, ch, slot, true)); }
int DcModels::wm_level_up(int ch, int slot, Object *walker) { load_tables(); return int(dccore::weapon_level_up(*menu_status(walker), *item_db_, rng_, ch, slot, true, save_.game_flag(0x30))); }
int DcModels::wm_status_break(int ch, int slot, Object *walker) { load_tables(); return int(dccore::weapon_status_break(*menu_status(walker), *item_db_, rng_, ch, slot)); }
int DcModels::wm_build_up(int ch, int slot, int target, Object *walker) { load_tables(); return int(dccore::weapon_build_up(*menu_status(walker), *item_db_, rng_, ch, slot, target)); }
int DcModels::wm_powder_count(int id, Object *walker) { return dccore::bag_count(*menu_status(walker), id); }

Array DcModels::wm_build_targets(int ch, int slot, Object *walker) {
    Array out;
    load_tables();
    for (const dccore::BuildTarget &t : dccore::build_up_targets(*menu_status(walker), *item_db_, rng_, ch, slot)) {
        Dictionary d;
        d["id"] = t.id;
        d["name"] = get_item_name(t.id);
        d["buildable"] = t.buildable;
        out.push_back(d);
    }
    return out;
}


Dictionary DcModels::peek_save(const String &path) {
    Dictionary d;
    d["exists"] = false;
    std::ifstream f(std::filesystem::path(path.utf8().get_data()), std::ios::binary);
    if (!f) return d;
    uint32_t header[3] = {};
    f.read(reinterpret_cast<char *>(header), sizeof header);
    if (!f || header[0] != 0x56534344u || header[1] != 3u) return d;
    int16_t deepest[8] = {};
    f.read(reinterpret_cast<char *>(deepest), sizeof deepest);
    dccore::SaveData sd;
    f.read(reinterpret_cast<char *>(sd.raw.data()), std::streamsize(sd.raw.size()));
    if (!f) return d;
    dccore::UserStatus st;
    sd.load_status(st);
    d["exists"] = true;
    d["day"] = sd.day();
    d["play_time"] = sd.play_time();
    d["gold"] = int(st.get<uint16_t>(0x4346));
    d["name"] = String::utf8(sd.hero_name(0).c_str());
    d["town"] = int(sd.get<int32_t>(0x30));
    return d;
}


Node3D *DcModels::build_opening() {
    if (!disc_) return nullptr;
    try {
        DcOpening *o = memnew(DcOpening);
        o->setup(disc_.get());
        return o;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.build_opening: ") + e.what());
        return nullptr;
    }
}


// The state a boss leaves behind (notes/dungeon_events.md 4.4, 5): the story flags the clear scene sets and where the game goes next. The scenes themselves are skipped.
void DcModels::boss_cleared(int map_no, int floor) {
    (void)floor;
    int map = 1, event = -1;
    switch (map_no) {
        case 0: save_.set_game_int(0, 100); map = 1; event = 300; break;                 // Dran: Norune, event 300 (the Worldmap)
        case 1: save_.set_game_flag(34, true); map = 15; event = -1; break;               // s04
        case 2: save_.set_game_int(0, 300); map = 45; event = 300; break;                 // s34
        case 3: save_.set_game_int(0, 400); map = 35; event = -1; break;                  // s24
        case 4: save_.set_game_flag(56, true); save_.set_game_int(0, 500); map = 5; event = 301; break;   // e05
        case 5:   // the last two floors are boss fights in sequence (game int 10 = the stage: 1 the first boss, 2 the second, 3 done)
            if (save_.game_int(10) <= 1) {
                save_.set_game_int(10, 2);
                save_.set_game_flag(41, true);
                map = -1;   // on to floor 25 of the same dungeon
                event = 25;
            } else {
                save_.set_game_int(10, 3);
                save_.set_game_int(0, 600);
                map = 50;
                event = 300;
            }
            break;                                                                        // s39
        default: save_.set_game_int(0, 700); give_item(298); map = 61; event = -1; break; // s50
    }
    clear_jump_ = Dictionary();
    clear_jump_["map"] = map;
    clear_jump_["event"] = event;
    if (map < 0) {   // (a floor of the same dungeon follows)
        clear_jump_["dungeon"] = map_no;
        clear_jump_["floor"] = event;
    }
}

String DcModels::article_text(int id) {
    load_tables();
    if (!article_text_) {
        try {
            article_text_ = std::make_shared<dccore::MessageBank>(dccore::MessageBank::parse(disc_->read("meswin/sysarticle_1.mes")));
        } catch (const std::exception &) {
            return String();
        }
    }
    return String::utf8(article_text_->get(id, kHeroNames).c_str());
}

// ---- fishing menus ----------------------------------------------------------------------------------------------------------------------------------------

Array DcModels::fish_baits() {
    Array out;
    load_tables();
    if (!fish_tables_) return out;
    for (int b = 0; b < 13; ++b) {
        const int id = fish_tables_->bait_item[b];
        const int n = dccore::bag_count(status_, id);
        if (n <= 0) continue;
        Dictionary d;
        d["id"] = id;
        d["count"] = n;
        d["name"] = get_item_name(id);
        out.push_back(d);
    }
    return out;
}

bool DcModels::fish_take_bait(int item) {
    if (dccore::bag_count(status_, item) <= 0) return false;
    dccore::bag_take_one(status_, item);
    return true;
}

int DcModels::fish_points() const { return dccore::fishing_points(save_); }

void DcModels::fish_set_points(int fp) { dccore::set_fishing_points(save_, fp); }
void DcModels::fish_add_record(int kind, int size10) { dccore::set_fishing_rank(save_, size10, kind); }

Array DcModels::fish_exchange_rows() {
    Array out;
    load_tables();
    if (!fish_tables_) return out;
    for (size_t i = 0; i < fish_tables_->exchange.size(); ++i) {
        const int id = fish_tables_->exchange[i][0], cost = fish_tables_->exchange[i][1];
        if (id <= 0) continue;
        if (id == 278 && (save_.game_flag(0xca) || dccore::mardan_caught(save_) <= 0)) continue;   // the Mardan rod: once, after the first Garayan
        Dictionary d;
        d["row"] = int(i);
        d["id"] = id;
        d["cost"] = cost;
        d["name"] = get_item_name(id);
        d["icon"] = get_item_icon(id);
        out.push_back(d);
    }
    return out;
}

int DcModels::fish_exchange(int row) {
    load_tables();
    if (!fish_tables_ || row < 0 || row >= 35) return 3;
    const int id = fish_tables_->exchange[size_t(row)][0], cost = fish_tables_->exchange[size_t(row)][1];
    if (id <= 0) return 3;
    if (dccore::fishing_points(save_) < cost) return 1;
    if (dccore::check_item_get(status_, id) != 0) return 2;
    dccore::get_item(status_, *item_db_, id, rng_);
    dccore::set_fishing_points(save_, dccore::fishing_points(save_) - cost);
    if (id == 278) {
        save_.set_game_flag(0xca, true);
        save_.set_game_int(0x14, 0);
    }
    return 0;
}

Array DcModels::fish_records() {
    Array out;
    load_tables();
    if (!fish_tables_) return out;
    for (int i = 0; i < 20; ++i) {
        int kind = 0, size10 = 0;
        if (!dccore::fishing_rank(save_, i, kind, size10)) break;
        Dictionary d;
        d["rank"] = i + 1;
        d["kind"] = kind;
        d["size"] = size10;
        d["name"] = String::utf8(system_text_->get(fish_tables_->fish[std::clamp(kind, 0, 17)].message + 30).c_str());
        out.push_back(d);
    }
    return out;
}

Node3D *DcModels::make_player_model(int character, int color, int weapon_id) {
    if (!disc_) return nullptr;
    try {
        if (dun_bin_.empty()) dun_bin_ = disc_->read_root("DUN.BIN");
        const int ch = std::clamp(character, 0, 5);
        auto kit = std::make_shared<dccore::PlayerKit>(dccore::load_player_kit(*disc_, dun_bin_, ch, weapon_id >= 0 ? weapon_id : dccore::kDefWeapon[ch] + 1, 0));
        DcCharacter *node = memnew(DcCharacter);
        Dictionary info;
        node->setup(kit->model, info);
        if (weapon_id >= 0 && kit->weapon_model) {
            DcCharacter *w = memnew(DcCharacter);
            Dictionary winfo;
            w->setup(kit->weapon_model, winfo);
            node->set_held_weapon(w, node->find_joint("weapon"));
            if (color >= 0) w->set_player_color(-1);
        }
        if (color >= 0) node->set_player_color(color);
        return node;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.make_player_model: ") + e.what());
        return nullptr;
    }
}

void DcModels::set_coop_floor(int64_t seed, const PackedByteArray &host_status) {
    coop_seed_ = seed;
    coop_host_status_.reset();
    if (host_status.size() == int64_t(dccore::UserStatus::kSize)) {
        coop_host_status_ = std::make_unique<dccore::UserStatus>();
        std::memcpy(coop_host_status_->raw.data(), host_status.ptr(), dccore::UserStatus::kSize);
    }
}

Dictionary DcModels::pad_state() const {
    const dc_pad::State p = dc_pad::read();
    Dictionary d;
    d["bits"] = int(p.bits);
    d["lx"] = p.lx; d["ly"] = p.ly; d["rx"] = p.rx; d["ry"] = p.ry;
    return d;
}

void DcModels::clear_coop_floor() {
    coop_seed_ = -1;
    coop_players_ = 1;
    coop_host_status_.reset();
}

PackedByteArray DcModels::get_status_bytes() const {
    PackedByteArray out;
    out.resize(int64_t(dccore::UserStatus::kSize));
    std::memcpy(out.ptrw(), status_.raw.data(), dccore::UserStatus::kSize);
    return out;
}

int DcModels::take_bag_item(int slot) {
    if (slot < 0 || slot >= dccore::kBagSlots) return -1;
    const int id = status_.get<int16_t>(0x436e + size_t(slot) * 2);
    if (id < 0x84 || id > 0x100) return -1;
    dccore::consume_bag_slot(status_, slot);
    return id;
}

PackedByteArray DcModels::get_town_layout(int town) const {
    PackedByteArray out;
    if (town < 0 || town > 5) return out;
    out.resize(int64_t(dccore::SaveData::kGrdSize));
    std::memcpy(out.ptrw(), save_.raw.data() + dccore::SaveData::kGrd + size_t(town) * dccore::SaveData::kGrdSize, dccore::SaveData::kGrdSize);
    return out;
}

void DcModels::set_town_layout_override(int town, const PackedByteArray &layout) {
    if (town < 0 || town > 5 || layout.size() != int64_t(dccore::SaveData::kGrdSize)) return;
    town_override_[town] = std::vector<uint8_t>(layout.ptr(), layout.ptr() + layout.size());
}

Node3D *DcModels::make_town_model(const String &pack_path, const String &cfg, int color) {
    if (!disc_) return nullptr;
    try {
        const std::string path = pack_path.utf8().get_data();
        if (!disc_->contains(path)) return nullptr;
        dccore::Pack pk(disc_->read(path));
        DcCharacter *node = memnew(DcCharacter);
        Dictionary info;
        node->setup(std::make_shared<dccore::Character>(dccore::load_character(pk, cfg.utf8().get_data())), info);
        if (color >= 0) node->set_player_color(color);
        return node;
    } catch (const std::exception &e) {
        UtilityFunctions::push_error(String("DcModels.make_town_model: ") + e.what());
        return nullptr;
    }
}
