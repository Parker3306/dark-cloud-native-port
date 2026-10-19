#pragma once
#include <set>
#include <string>
#include <map>
#include <memory>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <dccore/disc.h>
#include <dccore/dungeon.h>
#include <dccore/event_world.h>
#include <dccore/fishing.h>
#include <dccore/georama.h>
#include <dccore/items.h>
#include <dccore/player.h>
#include <dccore/savedata.h>
#include <dccore/shop.h>
#include <dccore/weapon_ops.h>
#include <dccore/tables.h>
#include <dccore/text.h>
#include <dccore/town.h>
#include <dccore/user_status.h>

namespace godot {

// Loads characters straight from the player's ISO (or from a .chr file) and turns them into Godot scene nodes.
// All parsing happens in dccore; this class only converts types.
class DcModels : public RefCounted {
    GDCLASS(DcModels, RefCounted)
    std::unique_ptr<dccore::Disc> disc_;
    std::unique_ptr<dccore::Image> item_sheet_, weapon_sheet_;
    std::map<int, Ref<ImageTexture>> item_icons_;
    std::map<std::string, Ref<ImageTexture>> pack_textures_;
    Dictionary last_info_;
    // ---- the running game (one player record and one random number generator carried from floor to floor)
    std::shared_ptr<dccore::GameTables> tables_;
    std::shared_ptr<dccore::EventTables> event_tables_;
    std::shared_ptr<dccore::ItemDb> item_db_;
    dccore::SaveData save_;   // the game's whole persistent state (status_ lives inside it when saved)
    dccore::Bytes dun_bin_;   // the dungeon overlay (DUN.BIN): the characters' effect rows live there
    dccore::UserStatus status_ = dccore::UserStatus::new_game();
    dccore::GameRand rng_;
    int16_t deepest_[7] = {-1, -1, -1, -1, -1, -1, -1};   // deepest floor reached per dungeon (-1: never visited)
    std::shared_ptr<dccore::MessageBank> system_text_, atla_text_, article_text_;
    std::shared_ptr<dccore::GeoTables> geo_tables_;
    std::shared_ptr<dccore::FishTables> fish_tables_;
    std::shared_ptr<dccore::ShopTables> shop_tables_;
    std::shared_ptr<dccore::MessageBank> shop_desc_, shopman_, menu_text_;   // + allmenu.mes (the menus' texts)
    dccore::UserStatus *menu_status(Object *walker);   // itemshop.bin (descriptions, UI texts) and shopman.bin (the shopkeepers' lines)
    bool story_next_ = false;                                 // the next floor built is a story room (its events are the dungeon script's)
    struct BackPackage { bool present = false; dccore::FloorLayout layout; dccore::FloorEvents events; };
    BackPackage back_next_;                                   // the back floor of the random floor being built (handed to the walker by assemble_walker)
    bool boss_next_ = false;                                  // the next floor built is a boss room
    Dictionary clear_jump_;
    void boss_cleared(int map_no, int floor);
    Dictionary item_entry(dccore::ItemRef ref, bool for_sale) const;
    void load_tables();
    std::set<std::string> script_names(int map_no);   // the names in the dungeon's event script (the frames _GET_OBJHDL can ask for)
    std::map<int, std::vector<uint8_t>> town_override_;
    std::string ruby_cfg_;     // the dungeon cfg whose Ruby door colour is current (BtCfgCash)
    int ruby_key_ = 0;         // BtRubyDoorKey: the element of the Ruby crystals of this cfg
    int64_t coop_seed_ = -1;
    std::unique_ptr<dccore::UserStatus> coop_host_status_;
    Node3D *assemble_walker(std::shared_ptr<dccore::Dungeon> dungeon, const dccore::FloorLayout &layout, int map_no, int floor, const String &character_game_path, bool monsters,
            uint32_t monster_rand_state, const dccore::FloorEvents *events, const dccore::UserStatus *status, int gate_key);

protected:
    static void _bind_methods();

public:
    bool open_iso(const String &path);
    bool has_file(const String &game_path) const;
    int get_file_count() const;
    Node3D *build_character(const String &game_path);       // e.g. "dun/mainchara/c01d.chr", read from the opened ISO
    Node3D *build_character_file(const String &disk_path);  // a pack on disk (for example one of our rebuilt packs)
    // A generated dungeon floor: `name` is the dungeon file stem (e.g. "d01main_a" -> dun/d01main_a.cfg + dun/mpd_pack/d01main_a.mpd).
    Node3D *build_dungeon(const String &name, int seed, int rooms, int map_no);
    // A playable floor: the floor, the character pack (e.g. "dun/mainchara/c01d.chr", or "" for none) and the game's camera; see DcWalker.
    // Co-op: the model of any playable character (the dungeon model, idle/run motions 0 and 1) wearing player `color`'s colour (-1: original); null when it cannot be read
    Node3D *make_town_model(const String &pack_path, const String &cfg, int color);   // a character pack of a town (the hero with his rod ...), coloured for a player; null when it cannot be read
    Node3D *make_player_model(int character, int color, int weapon_id = -1);   // weapon_id >= 0: he holds that weapon
    Node3D *build_walker(const String &name, int seed, int rooms, int map_no, const String &character_game_path, int floor = 0, bool monsters = true, int64_t arrange_seed = -1);
    // A town or story scene ("e01".., "s01"..) to walk through: the static objects and, when `complete` is set, the developer's finished layout (gdata0.edt).
    // Co-op: the host's Georama is the town everybody walks in (the 3000 bytes of the town in the save record: the parts and where they stand)
    PackedByteArray get_town_layout(int town) const;
    void set_town_layout_override(int town, const PackedByteArray &layout);
    void clear_town_layout_overrides() { town_override_.clear(); }
    Node3D *build_town(const String &name, bool complete, int start_event = -1, int old_map = 0);
    // ---- shops and the storage counter (dccore/shop.h)
    Array shop_stock(int shop);                              // [{id, name, price, kind, desc}]
    int shop_buy(int shop, int index);                       // ShopResult
    Array shop_inventory(bool for_sale);                     // the player's things: [{kind, owner, slot, id, name, price (-1: may not be sold), value, equipped}]
    int shop_sell(int kind, int owner, int slot);
    Array storage_list(int kind);                            // [{slot, id, name, value}] of the storage counter
    int storage_store(int kind, int owner, int slot);
    int storage_take(int kind, int slot);
    Dictionary storage_capacity(int kind);                   // {used, max}
    String shop_text(int id);                                // shopman.bin / itemshop.bin text
    String item_description(int id);
    String get_hero_name(int ch) const { return String::utf8(save_.hero_name(ch).c_str()); }
    void set_hero_name(int ch, const String &name) { save_.set_hero_name(ch, name.utf8().get_data()); }
    int get_option(int row) const;                           // the 12 settings of the option menu (rows 1..12, notes/menus_screens.md 1.1)
    void set_option(int row, int value);
    // ---- the weapon page (equip / customize / upgrade / status break / build-up): `walker` is the running floor (its record) or null (the town's record)
    String menu_text(int id);                                // allmenu.mes
    Array wm_weapons(int ch, Object *walker);                // [{slot, id, name, level, stats..., holes, elements, slayers, ...}]
    Array wm_attach_items(Object *walker);                   // [{slot, id, name, stats}]
    int wm_equip(int ch, int slot, Object *walker);          // the WeaponResult of each operation
    int wm_discard(int ch, int slot, Object *walker);
    int wm_element(int ch, int slot, int element, Object *walker);
    int wm_attach(int ch, int slot, int hole, int attach_slot, Object *walker);
    int wm_detach(int ch, int slot, int hole, Object *walker);
    int wm_repair(int ch, int slot, Object *walker);
    int wm_level_up(int ch, int slot, Object *walker);
    int wm_status_break(int ch, int slot, Object *walker);
    Array wm_build_targets(int ch, int slot, Object *walker);   // [{id, name, buildable}]
    int wm_build_up(int ch, int slot, int target, Object *walker);
    int wm_powder_count(int id, Object *walker);
    int get_party_size() const { return int(status_.get<int8_t>(5)); }
    int give_item(int id) { load_tables(); return dccore::get_item(status_, *item_db_, id, rng_); }   // for tests
    Dictionary peek_save(const String &path);                // {exists, day, play_time, gold, name, town}: what a save slot shows
    void set_last_town(int town) { save_.set<int32_t>(0x30, town); }   // config word +0x30 is unused by the game: it remembers the town the player was in
    int get_last_town() const { return int(save_.get<int32_t>(0x30)); }
    Dictionary get_clear_jump() const { return clear_jump_; }   // after a boss: {map, event} of the town or story map the game goes to
    void set_gold(int gold) { status_.set<uint16_t>(0x4346, uint16_t(gold)); }   // for tests
    void grant_atla(int town, bool install);                // gives the parts and tips of a town's Atla (for tests; install: also puts every tip in place)
    Node3D *build_opening();                                 // the opening cutscene (DcOpening)
    Node3D *build_interior(const String &town_name, const String &house, int start_event = -1, int variant = 0);   // a house: gedit/<town>/in/<house>/ (variant 0 day, 1 evening, 2 night)
    Node3D *make_town_walk(std::shared_ptr<dccore::Town> town, int map_index, int start_event, int old_map);
    Node *build_sound();   // the music / effects player (a DcSound node), null if the sound data cannot be read
    Dictionary get_last_info() const { return last_info_; }
    // ---- playing: new_game() starts from the first character and weapon; start_floor() draws the floor's seed from the game's generator, builds the floor with its
    // monsters, chests, statues and trigger areas and puts the player on it; end_floor() takes the player record and the generator back from the walker.
    void new_game(int64_t seed);
    Node3D *start_floor(const String &name, int map_no, int floor, int rooms, const String &character_game_path);
    // Co-op: every player builds the SAME floor. The host's seed and its player record (the Atla lists decide which statues the floor holds) are used for the next
    // start_floor; the player's own record is only played with. clear_coop_floor() goes back to the single player rules.
    void set_coop_floor(int64_t seed, const PackedByteArray &host_status);
    void clear_coop_floor();
    void enter_dungeon_reset() { ruby_cfg_.clear(); }   // a visit to a dungeon starts: the cfg cache is empty (BtMapJumpCashClear)
    PackedByteArray get_status_bytes() const;                    // the player record (what a host sends so the others can build the same floor)
    int64_t draw_coop_seed() { return int64_t(rng_.next() & 0x7fffffff); }   // a fresh floor seed for the others
    void end_floor(Node3D *walker);
    void game_over(Node3D *walker);    // the player died: SetDead (half of the gold gone), life restored
    Dictionary get_session_status() const;
    String article_text(int id);   // meswin/sysarticle_1.mes: the short messages of the dungeon (trap circles 300..309, "acquired", ...)
    void set_clear_jump(int map, int event) { clear_jump_ = Dictionary(); clear_jump_["map"] = map; clear_jump_["event"] = event; }   // a story script sent the player to another map
    // fishing: the bait list of the bag, the exchange of fishing points for items, the record list
    Array fish_baits();
    bool fish_take_bait(int item);
    int fish_points() const;
    void fish_set_points(int fp);   // tests
    void fish_add_record(int kind, int size10);   // tests
    Array fish_exchange_rows();
    int fish_exchange(int row);          // 0 done, 1 not enough points, 2 the bag is full, 3 nothing to buy
    Array fish_records();
    Dictionary get_floor_map(int dungeon, int floor);   // {name, kind}: the map file the dungeon's own script picks for that floor (kind 1 random, 0 story, 2 boss)
    // The same bag / weapon / rest operations as the walker offers, for use outside a dungeon (village menu).
    Array get_bag() const;
    Dictionary use_bag_item(int slot);
    int take_bag_item(int slot);                       // co-op gift: one of the bag slot leaves the record; returns its item id (-1: nothing there)
    Array get_weapons() const;
    bool equip_weapon(int slot);
    void rest();                            // everybody's life and water back to full
    int get_deepest_floor(int dungeon) const { return dungeon >= 0 && dungeon < 7 ? deepest_[dungeon] : -1; }
    Dictionary get_village_status() const;
    bool save_game(const String &path);    // the player record, the generator and the position in the dungeon (file path on disk)
    bool load_game(const String &path);
    Ref<ImageTexture> get_pack_texture(const String &pack_path, const String &texture_name);   // any texture of the image containers inside a pack (title art, menu art), cached
    Ref<ImageTexture> get_item_icon(int item);          // the 32x32 menu icon of an item (65..256), or null
    String get_item_name(int item);                       // name of a bag item, attach material or weapon (system text bank)
    String get_system_text(int id);                        // any message of meswin/_systeme.bin
    Array get_party() const;                             // [{character, name, joined, life, max_life, weapon_id}]
    bool switch_character(Node3D *walker_node, int character);   // in the middle of a floor: the party menu
    void join_party(int character);                      // a character joins with their first weapon
    void set_character(int character);                   // the character to play (they must have joined)
    int get_character() const { return status_.character(); }
    bool cheat_give_item(int item);                      // test helper: puts an item into the bag
};

} // namespace godot
