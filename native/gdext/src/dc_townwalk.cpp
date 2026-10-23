#include "dc_townwalk.h"
#include "dc_pad.h"
#include "dc_eb_ui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/input.hpp>
#include <dccore/tim2.h>
#include <godot_cpp/classes/input_event_key.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// what the event runtime asks of the front end

class godot::TownHost : public dccore::EventHost {
public:
    DcTownWalk *w;
    explicit TownHost(DcTownWalk *walk) : w(walk) {}

    bool load_pack(int index, const std::string &path) override {
        if (index < 0 || index >= 16) return false;
        if (w->event_pack_paths_.size() <= size_t(index)) w->event_pack_paths_.resize(size_t(index) + 1);
        w->event_pack_paths_[size_t(index)] = path;
        return w->pack_at(path) != nullptr;
    }

    bool load_actor(int slot, int pack, const std::string &entry) override {
        if (pack < 0 || size_t(pack) >= w->event_pack_paths_.size()) return false;
        auto p = w->pack_at(w->event_pack_paths_[size_t(pack)]);
        if (!p) return false;
        try {
            if (const dccore::PackEntry *cfg = p->find(entry)) {
                const dccore::View text = p->data(*cfg);
                if (!dccore::cfg_has_model(text)) {   // a config that only adds motions to the model already there
                    DcCharacter *target = w->actor_nodes_.count(slot) ? w->actor_nodes_[slot] : (slot == -1 ? w->player_ : nullptr);
                    if (target && target->get_character()) {
                        auto copy = std::make_shared<dccore::Character>(*target->get_character());
                        dccore::add_motion_sets(*copy, *p, text);
                        target->set_character(copy);
                    }
                    return true;
                }
            }
            auto ch = std::make_shared<dccore::Character>(dccore::load_character(*p, entry));
            if (ch->model_name.empty()) return true;
            DcCharacter *node = w->make_actor_node(ch);
            if (slot == -1) {   // a different model for the hero during the event
                if (w->actor_nodes_.count(-1)) { w->actor_nodes_[-1]->queue_free(); w->remove_child(w->actor_nodes_[-1]); }
                w->actor_nodes_[-1] = node;
                if (w->player_) w->player_->set_visible(false);
            } else {
                if (w->actor_nodes_.count(slot)) { w->remove_child(w->actor_nodes_[slot]); w->actor_nodes_[slot]->queue_free(); }
                w->actor_nodes_[slot] = node;
            }
            return true;
        } catch (const std::exception &) {
            // a config without a model of its own adds motions to the model already there
            return true;
        }
    }

    bool load_scene(int scene, int pack, const std::string &entry, float speed) override {
        if (scene < 0 || scene >= 4 || pack < 0 || size_t(pack) >= w->event_pack_paths_.size()) return false;
        auto p = w->pack_at(w->event_pack_paths_[size_t(pack)]);
        if (!p) return false;
        try {
            auto ch = std::make_shared<dccore::Character>(dccore::load_character(*p, entry));
            if (w->scene_nodes_[scene]) { w->remove_child(w->scene_nodes_[scene]); w->scene_nodes_[scene]->queue_free(); }
            DcCharacter *node = w->make_actor_node(ch);
            node->set_playing(false);
            w->scene_nodes_[scene] = node;
            const float start = ch->motions.empty() ? 0.0f : ch->motions[0].start;
            w->scene_time_[scene] = double(start);
            w->scene_step_[scene] = speed > 0.0f ? speed : (ch->motions.empty() || ch->motions[0].speed <= 0.0f ? 1.0f : ch->motions[0].speed);
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }

    void load_textures(int block, int pack, const std::vector<std::string> &entries) override {
        (void)block;
        if (pack < 0 || size_t(pack) >= w->event_pack_paths_.size()) return;
        auto p = w->pack_at(w->event_pack_paths_[size_t(pack)]);
        if (!p) return;
        for (const std::string &name : entries) {
            const dccore::PackEntry *e = p->find(name);
            if (!e) continue;
            try {
                const dccore::View data = p->data(*e);
                for (const dccore::ImgTexture &t : dccore::img_textures(data)) {
                    const dccore::Image im = dccore::Tim2(data, t.offset).decode(false, t.swizzled);
                    PackedByteArray bytes;
                    bytes.resize(int64_t(im.rgba.size()));
                    std::memcpy(bytes.ptrw(), im.rgba.data(), im.rgba.size());
                    Ref<Image> img = Image::create_from_data(int(im.width), int(im.height), false, Image::FORMAT_RGBA8, bytes);
                    w->sprite_textures_[t.name] = ImageTexture::create_from_image(img);
                }
            } catch (const std::exception &) {
            }
        }
    }

    bool load_main_chara(const std::string &file, const std::string &cfg, int mode) override {
        (void)mode;
        // the hero's model of the event; the normal one ("chara/c01d.chr") means the player's own model again
        const std::string base = file.size() >= 4 ? file.substr(file.size() - 4) : "";
        if (file.find("_turi") == std::string::npos && base == ".chr") {
            if (w->actor_nodes_.count(-1)) { w->remove_child(w->actor_nodes_[-1]); w->actor_nodes_[-1]->queue_free(); w->actor_nodes_.erase(-1); }
            if (w->player_) w->player_->set_visible(true);
            return true;
        }
        auto p = w->pack_at(file);
        if (!p) return false;
        try {
            auto ch = std::make_shared<dccore::Character>(dccore::load_character(*p, cfg));
            DcCharacter *node = w->make_actor_node(ch);
            if (w->actor_nodes_.count(-1)) { w->remove_child(w->actor_nodes_[-1]); w->actor_nodes_[-1]->queue_free(); }
            w->actor_nodes_[-1] = node;
            if (w->hero_color_ >= 0) node->set_player_color(w->hero_color_);
            if (w->player_) w->player_->set_visible(false);
            return true;
        } catch (const std::exception &) {
            return false;
        }
    }

    void unload_actor(int slot) override {
        auto it = w->actor_nodes_.find(slot);
        if (it == w->actor_nodes_.end()) return;
        w->remove_child(it->second);
        it->second->queue_free();
        w->actor_nodes_.erase(it);
        if (slot == -1 && w->player_) w->player_->set_visible(true);
    }

    void load_villager(int slot, int info_id, const std::string &file) override {
        (void)info_id;
        auto p = w->pack_at(file);
        if (!p) return;
        try {
            auto ch = std::make_shared<dccore::Character>(dccore::load_character(*p));
            DcCharacter *node = w->make_actor_node(ch);
            if (w->actor_nodes_.count(slot)) { w->remove_child(w->actor_nodes_[slot]); w->actor_nodes_[slot]->queue_free(); }
            w->actor_nodes_[slot] = node;
        } catch (const std::exception &) {
        }
    }

    void open_modal(int kind, const std::vector<int> &args) override {
        w->modal_request_["kind"] = kind;
        Array a;
        for (int v : args) a.push_back(v);
        w->modal_request_["args"] = a;
    }
    int villager_present(int vil) override { return w->villager_present_now(vil) ? 1 : 0; }
    void villager_appear(int op, int vil) override {
        w->villager_override_[vil] = (op == 0 || op == 2) ? 1 : 0;
        for (size_t i = 0; i < w->town_->people.size(); ++i)
            if (w->town_->people[i].villager == vil && i < w->villager_visible_.size()) w->villager_visible_[i] = op == 0 || op == 2;
    }
    void villagers_load() override {
        for (size_t i = 0; i < w->town_->people.size(); ++i)
            if (i < w->villager_visible_.size()) w->villager_visible_[i] = w->villager_present_now(w->town_->people[i].villager);
        w->init_roamers();
    }
    int villager_part(int vil) override { return w->geo_ ? w->geo_->villager_part(vil) : -1; }
    int georama_complete(int parts) override { return w->geo_ && w->geo_->complete(parts) ? 1 : 0; }
    int georama_cmp_event(int parts) override { return w->geo_ && w->geo_->comp_event(parts) ? 1 : 0; }
    void georama_set_cmp_event(int parts, int v) override { if (w->geo_) w->geo_->set_comp_event(parts, v != 0); }
    int georama_request(int parts) override { return w->geo_ && w->geo_->request(parts) ? 1 : 0; }
    int georama_atra_parts(int map1) override { return w->env_.save ? dccore::georama_atra_parts_count(*w->env_.save, map1 - 1) : 0; }
    int georama_request_percent(int town1) override { return w->geo_ && w->geo_->town_no() == town1 - 1 ? w->geo_->request_percent() : 0; }
    void georama_atra_get(int kind, int town1, int id) override {
        if (w->env_.save && w->env_.geo_tables) dccore::georama_atra_get(*w->env_.save, *w->env_.geo_tables, town1 - 1, kind == 0 ? id : id + 0x28);
    }
    void georama_set_request_flag(int town1, int v) override { if (w->geo_ && w->geo_->town_no() == town1 - 1) w->geo_->set_request_event_flag(v != 0); }
    bool georama_parts_pos(int obj, dccore::EvVec3 &pos, dccore::EvVec3 &rot) override {   // GetPartsObject: the lowest slot that holds a part of that type
        for (const dccore::EditSlot &s : w->town_->slots)
            if (s.used && s.def == obj && s.instance >= 0) {
                const dccore::TownInstance &in = w->town_->instances[size_t(s.instance)];
                pos = {in.pos[0], in.pos[1], in.pos[2]};
                rot = {0, in.yaw, 0};
                return true;
            }
        return false;
    }

    void sound(const char *op, int a, int b, const float *v) override {
        const std::string o = op;
        if (o == "_LOAD_BGM") w->bgm_request_ = a;
        Dictionary d;
        d["op"] = String(op);
        d["a"] = a;
        d["b"] = b;
        Array va;
        for (int k = 0; k < 6; ++k) va.push_back(v ? double(v[k]) : 0.0);
        d["v"] = va;
        w->sound_events_.push_back(d);
    }
    int bgm_playing() override { return w->bgm_request_ >= 0 ? w->bgm_request_ : (w->town_ ? w->town_->info.bgm : -1); }

    bool actor_motion_info(int slot, int motion, float &start, float &end, float &blend) override {
        DcCharacter *n = slot == -1 && w->actor_nodes_.count(-1) == 0 ? w->player_ : (w->actor_nodes_.count(slot) ? w->actor_nodes_[slot] : nullptr);
        if (!n) return false;
        Dictionary d = n->get_motion_info(motion);
        if (!d.has("end")) return false;
        start = float(double(d["start"]));
        end = float(double(d["end"]));
        blend = 2.0f;
        return true;
    }
    bool actor_motion_range(int slot, int motion, float &start, float &end, float &speed) override {
        DcCharacter *n = slot == -1 && w->actor_nodes_.count(-1) == 0 ? w->player_ : (w->actor_nodes_.count(slot) ? w->actor_nodes_[slot] : nullptr);
        if (!n) return false;
        const Dictionary d = n->get_motion_info(motion);
        if (!d.has("end")) return false;
        start = float(double(d["start"]));
        end = float(double(d["end"]));
        speed = d.has("speed") && double(d["speed"]) > 0.0 ? float(double(d["speed"])) : 1.0f;
        return true;
    }
    float actor_motion_time(int slot) override {
        DcCharacter *n = slot == -1 && w->actor_nodes_.count(-1) == 0 ? w->player_ : (w->actor_nodes_.count(slot) ? w->actor_nodes_[slot] : nullptr);
        return n ? float(n->get_time()) : 0.0f;
    }
};

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

void DcTownWalk::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_script_ticks", "ticks"), &DcTownWalk::set_script_ticks);
    ClassDB::bind_method(D_METHOD("set_use_devices", "on"), &DcTownWalk::set_use_devices);
    ClassDB::bind_method(D_METHOD("teleport", "x", "y", "z"), &DcTownWalk::teleport);
    ClassDB::bind_method(D_METHOD("step_ticks", "n"), &DcTownWalk::step_ticks);
    ClassDB::bind_method(D_METHOD("get_player_position"), &DcTownWalk::get_player_position);
    ClassDB::bind_method(D_METHOD("set_hero_model", "model"), &DcTownWalk::set_hero_model);
    ClassDB::bind_method(D_METHOD("set_hero_color", "index"), &DcTownWalk::set_hero_color);
    ClassDB::bind_method(D_METHOD("get_hero_pose"), &DcTownWalk::get_hero_pose);
    ClassDB::bind_method(D_METHOD("get_camera"), &DcTownWalk::get_camera);
    ClassDB::bind_method(D_METHOD("get_scene"), &DcTownWalk::get_scene);
    ClassDB::bind_method(D_METHOD("get_tick_count"), &DcTownWalk::get_tick_count);
    ClassDB::bind_method(D_METHOD("take_nav"), &DcTownWalk::take_nav);
    ClassDB::bind_method(D_METHOD("get_near_event"), &DcTownWalk::get_near_event);
    ClassDB::bind_method(D_METHOD("set_free_camera", "pos", "target"), &DcTownWalk::set_free_camera);
    ClassDB::bind_method(D_METHOD("get_ui"), &DcTownWalk::get_ui);
    ClassDB::bind_method(D_METHOD("get_eb_ui"), &DcTownWalk::get_eb_ui);
    ClassDB::bind_method(D_METHOD("debug_eb_autoplay", "offset"), &DcTownWalk::debug_eb_autoplay);
    ClassDB::bind_method(D_METHOD("get_villagers"), &DcTownWalk::get_villagers);
    ClassDB::bind_method(D_METHOD("get_town_events"), &DcTownWalk::get_town_events);
    ClassDB::bind_method(D_METHOD("set_fish_auto", "on"), &DcTownWalk::set_fish_auto);
    ClassDB::bind_method(D_METHOD("get_mode"), &DcTownWalk::get_mode);
    ClassDB::bind_method(D_METHOD("get_bgm_request"), &DcTownWalk::get_bgm_request);
    ClassDB::bind_method(D_METHOD("take_sound_events"), &DcTownWalk::take_sound_events);
    ClassDB::bind_method(D_METHOD("set_bgm_number", "n"), &DcTownWalk::set_bgm_number);
    ClassDB::bind_method(D_METHOD("get_clock"), &DcTownWalk::get_clock);
    ClassDB::bind_method(D_METHOD("run_event", "id"), &DcTownWalk::run_event);
    ClassDB::bind_method(D_METHOD("run_record", "record", "arg"), &DcTownWalk::run_record);
    ClassDB::bind_method(D_METHOD("place_hero", "x", "y", "z", "yaw"), &DcTownWalk::place_hero);
    ClassDB::bind_method(D_METHOD("get_door_out", "event_index"), &DcTownWalk::get_door_out);
    ClassDB::bind_method(D_METHOD("take_modal_request"), &DcTownWalk::take_modal_request);
    ClassDB::bind_method(D_METHOD("modal_done", "value"), &DcTownWalk::modal_done);
    ClassDB::bind_method(D_METHOD("refresh_names"), &DcTownWalk::refresh_names);
    ClassDB::bind_method(D_METHOD("take_shop_request"), &DcTownWalk::take_shop_request);
    ClassDB::bind_method(D_METHOD("shop_done"), &DcTownWalk::shop_done);
    ClassDB::bind_method(D_METHOD("can_edit"), &DcTownWalk::can_edit);
    ClassDB::bind_method(D_METHOD("set_edit_locked", "locked"), &DcTownWalk::set_edit_locked);
    ClassDB::bind_method(D_METHOD("get_edit_ui"), &DcTownWalk::get_edit_ui);
    ClassDB::bind_method(D_METHOD("take_edit_notices"), &DcTownWalk::take_edit_notices);
    ClassDB::bind_method(D_METHOD("edit_toggle"), &DcTownWalk::edit_toggle);
    ClassDB::bind_method(D_METHOD("edit_menu_done", "result"), &DcTownWalk::edit_menu_done);
    ClassDB::bind_method(D_METHOD("georama_board"), &DcTownWalk::georama_board);
    ClassDB::bind_method(D_METHOD("georama_tray"), &DcTownWalk::georama_tray);
    ClassDB::bind_method(D_METHOD("georama_hand"), &DcTownWalk::georama_hand);
    ClassDB::bind_method(D_METHOD("georama_press_slot", "part", "slot"), &DcTownWalk::georama_press_slot);
    ClassDB::bind_method(D_METHOD("georama_press_tray", "cell"), &DcTownWalk::georama_press_tray);
    ClassDB::bind_method(D_METHOD("georama_cancel_hand"), &DcTownWalk::georama_cancel_hand);
    ClassDB::bind_method(D_METHOD("georama_sort_tray"), &DcTownWalk::georama_sort_tray);
    ClassDB::bind_method(D_METHOD("georama_select_part", "part"), &DcTownWalk::georama_select_part);
    ClassDB::bind_method(D_METHOD("georama_analysis"), &DcTownWalk::georama_analysis);
    ClassDB::bind_method(D_METHOD("georama_chip_name", "chip", "description"), &DcTownWalk::georama_chip_name);
    ClassDB::bind_method(D_METHOD("georama_text", "id"), &DcTownWalk::georama_text);
}

DcTownWalk::DcTownWalk() {}
DcTownWalk::~DcTownWalk() {}

std::shared_ptr<dccore::Pack> DcTownWalk::pack_at(const std::string &path) {
    auto it = packs_.find(path);
    if (it != packs_.end()) return it->second;
    std::shared_ptr<dccore::Pack> p;
    try {
        if (env_.disc && env_.disc->contains(path)) p = std::make_shared<dccore::Pack>(env_.disc->read(path));
    } catch (const std::exception &) {
    }
    packs_[path] = p;
    return p;
}

DcCharacter *DcTownWalk::make_actor_node(const std::shared_ptr<dccore::Character> &ch) {
    DcCharacter *node = memnew(DcCharacter);
    Dictionary info;
    node->setup(ch, info);
    add_child(node);
    return node;
}

void DcTownWalk::refresh_names() {
    if (!rt_ || !env_.save) return;
    for (int i = 0; i < 6; ++i) rt_->hero_names[size_t(i)] = env_.save->hero_name(i);
    rt_->msg_speed = env_.save->get<int32_t>(0x10) != 0 ? 0.6f : 0.3f;   // the option MESSAGE DSPLY SPEED
}

double DcTownWalk::get_clock() const { return env_.save ? double(env_.save->now_time()) : 0.0; }

void DcTownWalk::setup(DcTown *scene, DcCharacter *player, const TownEnv &env, int start_event) {
    scene_ = scene;
    town_ = scene->town();
    player_ = player;
    env_ = env;
    walker_ = std::make_unique<dccore::Walker>(nullptr, nullptr);
    walker_->params.mask = 0;   // EdMoveChara calls MoveCheck with mask 0 (t2 = $zero at 0x16a9b4): the polygons of placed parts and static objects are type 1, a dungeon player (mask 1) would walk through them
    const std::shared_ptr<dccore::Town> town = town_;
    walker_->poly_source = [town](float x, float y, float z) { return dccore::town_polys_near(*town, x, y, z); };
    walker_->camera_source = [town](float x, float y, float z) { return dccore::town_camera_polys(*town, x, y, z); };
    add_child(scene_);
    if (player_) add_child(player_);
    camera_ = memnew(Camera3D);
    camera_->set_fov(31.3);
    camera_->set_far(20000.0);
    camera_->set_near(2.0);
    add_child(camera_);
    // the map's event script
    host_ = std::make_unique<TownHost>(this);
    fish_sound = [this](int id) {   // the sounds of the rod
        Dictionary d;
        d["op"] = String("_PLAY_SE");
        d["a"] = id;
        d["b"] = 0;
        Array va;
        for (int k = 0; k < 6; ++k) va.push_back(0.0);
        d["v"] = va;
        sound_events_.push_back(d);
    };
    const std::string dir = "gedit/" + town_->name + "/";
    if (env_.disc && env_.disc->contains(dir + "event.stb")) {
        auto prog = std::make_shared<dccore::ScriptProgram>(env_.disc->read(dir + "event.stb"));
        rt_ = std::make_unique<dccore::EventRuntime>(prog, host_.get());
        rt_->save = env_.save;
        rt_->status = env_.status;
        rt_->items = env_.items;
        rt_->rng = env_.rng;
        rt_->map_no = env_.map_index;
        rt_->old_map_no = env_.old_map;
        rt_->map_dir = dir;
        refresh_names();
        rt_->interior_flag = town_->interior ? 1 : 0;
        if (env_.system_text) rt_->system_bank = *env_.system_text;
        if (env_.disc->contains(dir + town_->name + "_1.mes")) { rt_->event_bank = dccore::MessageBank::parse(env_.disc->read(dir + town_->name + "_1.mes")); rt_->has_event_bank = true; }
        if (env_.disc->contains(dir + town_->name + "talk_1.mes")) { rt_->talk_bank = dccore::MessageBank::parse(env_.disc->read(dir + town_->name + "talk_1.mes")); rt_->has_talk_bank = true; }
    }
    if (env_.save)   // the first visit sets the map flags of the points (LoadPTS): active unless the record says otherwise
        for (const dccore::TownEvent &e : town_->events)
            if (e.flag > 0 && !env_.save->map_init_flag(env_.map_index, e.flag)) {
                env_.save->set_map_init_flag(env_.map_index, e.flag, true);
                env_.save->set_map_flag(env_.map_index, e.flag, !e.initially_active);
            }
    for (size_t i = 0; i < town_->instances.size(); ++i) {
        if (town_->instances[i].owner_object >= 0) fixed_instances_.push_back(int(i));
        else if (town_->instances[i].part_def >= 0) placed_instances_.push_back(int(i));
    }
    if (env_.geo_tables && env_.save && env_.map_index >= 0 && env_.map_index <= 4) {   // the Georama rules of the town (the houses use them for the wishes and the tips)
        geo_ = std::make_unique<dccore::Georama>(*town_, *env_.save, *env_.geo_tables, env_.map_index);
        if (!town_->interior) { geo_->request_check(); apply_element_frames(); }
    }
    make_villagers();
    // start next to the first door out of the village until the entry event places the hero
    float sx = 0, sz = 0;
    for (const dccore::TownEvent &e : town_->events)
        if (e.kind == dccore::TownEvent::MapJump && e.map == "dungeon") { sx = e.pos[0] - 30.0f; sz = e.pos[2]; break; }
    walker_->place_at(sx, sz, 60.0f);
    if (town_->interior) {   // the hero enters through the door: just inside it, looking into the room
        for (const dccore::TownEvent &e : town_->events)
            if (e.kind == dccore::TownEvent::MapJump) {
                walker_->place_at(e.pos[0] - std::sin(e.yaw) * 18.0f, e.pos[2] - std::cos(e.yaw) * 18.0f, e.pos[1] + 30.0f);
                walker_->player.yaw = e.yaw + 3.14159265f;
                break;
            }
    }
    set_physics_process(true);
    if (rt_ && start_event != -2) pending_event_ = start_event >= 0 ? start_event : 128;   // -2: no entry scene (back from a dungeon, a loaded game)
    apply_state();
}

// villagers standing in the town
void DcTownWalk::make_villagers() {
    for (auto &row : phase_rows_) row.clear();
    if (!town_->interior && env_.rng) {   // EdCreateVillagerTable: the people list is shuffled (100 swaps per part of the day) and replaces the time table
        std::vector<int> list;
        for (int v : town_->info.people_list) if (v >= 0) list.push_back(v);
        const size_t n = list.size();
        if (n > 0)
            for (auto &row : phase_rows_) {
                for (int k = 0; k < 100; ++k) {
                    const size_t a = size_t(uint32_t(env_.rng->next()) % uint32_t(n)), b = size_t(uint32_t(env_.rng->next()) % uint32_t(n));
                    std::swap(list[a], list[b]);
                }
                row.assign(list.begin(), list.begin() + long(std::min<size_t>(n, 10)));
            }
    }
    villager_nodes_.assign(town_->people.size(), nullptr);
    villager_visible_.assign(town_->people.size(), false);
    for (size_t i = 0; i < town_->people.size(); ++i) {
        const dccore::TownPerson &p = town_->people[i];
        if (p.villager < 0) continue;
        const std::string model = size_t(p.villager) < town_->villager_names.size() ? town_->villager_names[size_t(p.villager)] : "";
        auto pk = pack_at("gedit/" + (town_->interior ? town_->parent : town_->name) + "/chara/" + model + ".chr");
        if (!pk) continue;
        try {
            auto ch = std::make_shared<dccore::Character>(dccore::load_character(*pk));
            DcCharacter *node = make_actor_node(ch);
            node->set_position(Vector3(p.pos[0], p.pos[1], p.pos[2]));
            node->set_rotation(Vector3(0, p.yaw, 0));
            villager_nodes_[i] = node;
            villager_visible_[i] = villager_present_now(p.villager);
        } catch (const std::exception &) {
        }
    }
    init_roamers();
}

// EdInitVilagerPosition (towns): half of the present villagers are pinned to their standing point (the ones that never move first, then random ones), the others roam
// from a random free cell near their house.
void DcTownWalk::init_roamers() {
    const size_t n = town_->people.size();
    roam_.assign(n, dccore::Roamer());
    roam_motion_.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const dccore::TownPerson &p = town_->people[i];
        dccore::Roamer &r = roam_[i];
        for (int k = 0; k < 3; ++k) r.pos[k] = p.pos[k];
        r.yaw = p.yaw;
        r.present = i < villager_visible_.size() && villager_visible_[i];
    }
    if (town_->interior || !geo_ || !env_.rng) return;
    std::vector<size_t> present;
    for (size_t i = 0; i < n; ++i)
        if (roam_[i].present && town_->people[i].villager >= 0) present.push_back(i);
    const size_t stat_quota = present.size() / 2;
    std::vector<bool> pinned(n, false);
    size_t count = 0;
    for (size_t i : present) {   // those that never move
        if (count >= stat_quota) break;
        const dccore::GeoCharaRow *row = geo_->chara_row(town_->people[i].villager);
        if (row && row->moves == 0) { pinned[i] = true; ++count; }
    }
    dccore::GameRand &rng = *env_.rng;
    while (count < stat_quota) {   // then random ones
        std::vector<size_t> pool;
        for (size_t i : present) if (!pinned[i]) pool.push_back(i);
        if (pool.empty()) break;
        pinned[pool[size_t(uint32_t(rng.next()) % uint32_t(pool.size()))]] = true;
        ++count;
    }
    std::vector<std::array<float, 3>> avoid;
    for (size_t i : present) avoid.push_back({town_->people[i].pos[0], town_->people[i].pos[1], town_->people[i].pos[2]});
    if (walker_) avoid.push_back({walker_->player.pos[0], walker_->player.pos[1], walker_->player.pos[2]});
    for (size_t i : present) {
        if (pinned[i]) continue;
        const dccore::GeoCharaRow *row = geo_->chara_row(town_->people[i].villager);
        if (!row || row->moves == 0) continue;
        // the house: the placed part of that type
        float centre[3] = {0, 0, 0};
        bool have_house = false;
        for (const dccore::EditSlot &s : town_->slots)
            if (s.used && s.def == row->part) { for (int k = 0; k < 3; ++k) centre[k] = s.pos[k]; have_house = true; break; }
        float spot[3];
        bool ok = false;
        float radius = have_house ? 200.0f : -1.0f;
        while (!ok) {
            ok = dccore::roam_random_plane_pos(*town_, centre, radius, avoid, 200.0f, rng, spot);
            if (ok || radius < 0.0f) break;
            radius += 100.0f;
            if (radius > 500.0f) radius = -1.0f;
        }
        if (!ok) continue;
        dccore::Roamer &r = roam_[i];
        r.roamer = true;
        r.speed = row->speed > 0.0f ? row->speed : 0.3f;
        for (int k = 0; k < 3; ++k) r.pos[k] = spot[k];
        avoid.push_back({spot[0], spot[1], spot[2]});
    }
}

// EdMoveVillager for the roamers: pause while the hero stands within 20 units, otherwise pick a cell when idle and walk one step.
void DcTownWalk::step_roamers() {
    if (!walker_) return;
    const float *hero = walker_->player.pos;
    for (size_t i = 0; i < roam_.size() && i < villager_nodes_.size(); ++i) {
        dccore::Roamer &r = roam_[i];
        if (!r.roamer) continue;
        r.present = i < villager_visible_.size() && villager_visible_[i];
        if (!r.present) continue;
        const float dx = hero[0] - r.pos[0], dy = hero[1] - r.pos[1], dz = hero[2] - r.pos[2];
        const bool paused = (talk_person_ == int(i) && mode_ != ModeWalk) || std::sqrt(dx * dx + dy * dy + dz * dz) < 20.0f;
        if (paused) {
            r.moving = false;
        } else {
            if (r.command == 0 && env_.rng) dccore::roam_decide(*town_, roam_, i, *env_.rng);
            dccore::roam_step(r);
        }
        dccore::TownPerson &p = town_->people[i];
        for (int k = 0; k < 3; ++k) p.pos[k] = r.pos[k];
        p.yaw = r.yaw;
        if (DcCharacter *node = villager_nodes_[i]) {
            node->set_position(Vector3(r.pos[0], r.pos[1], r.pos[2]));
            node->set_rotation(Vector3(0, r.yaw, 0));
            const int motion = r.moving ? 1 : 0;
            if (roam_motion_[i] != motion) {
                roam_motion_[i] = motion;
                node->play(motion, true);
            }
        }
    }
}

bool DcTownWalk::villager_present_now(int vil) const {
    if (town_->interior) return true;
    auto ov = villager_override_.find(vil);
    if (ov != villager_override_.end()) return ov->second != 0;
    const int bucket = rt_ ? rt_->time_bucket() : 0;
    if (!phase_rows_[size_t(bucket)].empty()) {   // the first six of the part of the day's row stand outdoors (EdSelectVillager)
        const auto &row = phase_rows_[size_t(bucket)];
        bool listed = false;
        for (size_t k = 0; k < row.size() && k < 6; ++k) if (row[k] == vil) { listed = true; break; }
        return listed && (!geo_ || geo_->villager_present(vil));
    }
    auto it = town_->info.time_table.find({0, bucket});
    if (it == town_->info.time_table.end()) return false;
    bool listed = false;
    for (int v : it->second) if (v == vil) { listed = true; break; }
    return listed && (!geo_ || geo_->villager_present(vil));
}

int DcTownWalk::villager_info_of(int person) const { return person >= 0 && size_t(person) < town_->people.size() ? town_->people[size_t(person)].villager : -1; }

dccore::PadRaw DcTownWalk::read_devices(uint32_t &menu_bits) const {
    dccore::PadRaw pad;
    menu_bits = 0;
    Input *in = Input::get_singleton();
    auto to_raw = [](float v) { return uint8_t(std::clamp(int(std::lround(128.0 + v * 127.0)), 0, 255)); };
    const dc_pad::State joy = dc_pad::read();   // the DualShock: the PS2 pad word as the game reads it (dc_pad.h), the same buttons as in PCSX2
    float lx = joy.lx, ly = joy.ly;
    const bool editing = mode_ == ModeEdit;
    auto key = [in](Key k) { return in->is_physical_key_pressed(k); };
    if (key(KEY_A) || (!editing && key(KEY_LEFT))) lx -= 1.0f;
    if (key(KEY_D) || (!editing && key(KEY_RIGHT))) lx += 1.0f;
    if (key(KEY_W) || (!editing && key(KEY_UP))) ly -= 1.0f;
    if (key(KEY_S) || (!editing && key(KEY_DOWN))) ly += 1.0f;
    pad.rx = to_raw(std::clamp(joy.rx, -1.0f, 1.0f));
    pad.ry = to_raw(std::clamp(joy.ry, -1.0f, 1.0f));
    if (editing) {   // the arrow keys are the D-pad: one cell at a time
        if (key(KEY_UP) || (joy.bits & 0x1000)) menu_bits |= 0x1000;
        if (key(KEY_DOWN) || (joy.bits & 0x4000)) menu_bits |= 0x4000;
        if (key(KEY_LEFT) || (joy.bits & 0x8000)) menu_bits |= 0x8000;
        if (key(KEY_RIGHT) || (joy.bits & 0x2000)) menu_bits |= 0x2000;
        if (key(KEY_G) || (joy.bits & 0x80)) pad.buttons |= dccore::kPadUse;
        if (key(KEY_Z) || (joy.bits & 0x01)) pad.buttons |= 1;
        if (key(KEY_C) || (joy.bits & 0x02)) pad.buttons |= 2;
    } else {
        if (key(KEY_W) || key(KEY_UP) || (joy.bits & 0x1000)) menu_bits |= 0x1000;
        if (key(KEY_S) || key(KEY_DOWN) || (joy.bits & 0x4000)) menu_bits |= 0x4000;
        if (joy.bits & 0x8000) menu_bits |= 0x8000;
        if (joy.bits & 0x2000) menu_bits |= 0x2000;
        pad.buttons |= joy.bits & 0x0F;   // L2 / R2 / L1 / R1 as the game gets them
    }
    if (key(KEY_T) || (joy.bits & 0x10)) menu_bits |= 0x10;
    if (!editing && (key(KEY_Z) || (joy.bits & 0x80))) menu_bits |= 0x80;   // square (the EB duel)
    if (key(KEY_B) || (joy.bits & 0x100)) menu_bits |= 0x100;
    if (joy.bits & 0x800) menu_bits |= 0x800;   // start
    pad.lx = to_raw(std::clamp(lx, -1.0f, 1.0f));
    pad.ly = to_raw(std::clamp(ly, -1.0f, 1.0f));
    if (in->is_physical_key_pressed(KEY_Q)) pad.buttons |= dccore::kPadCamLeft;
    if (in->is_physical_key_pressed(KEY_E)) pad.buttons |= dccore::kPadCamRight;
    if (!editing && in->is_physical_key_pressed(KEY_R)) pad.buttons |= dccore::kPadBehind;
    if (in->is_physical_key_pressed(KEY_SPACE) || in->is_physical_key_pressed(KEY_ENTER) || in->is_physical_key_pressed(KEY_F) || (joy.bits & 0x40)) pad.buttons |= dccore::kPadOK;
    if (in->is_physical_key_pressed(KEY_X) || in->is_physical_key_pressed(KEY_ESCAPE) || (joy.bits & 0x20)) pad.buttons |= dccore::kPadNO;
    return pad;
}

void DcTownWalk::set_script_ticks(const Array &ticks) {
    script_.clear();
    script_bits_.clear();
    for (int i = 0; i < ticks.size(); ++i) {
        Array t = ticks[i];
        script_bits_.push_back(t.size() > 5 ? uint32_t(int(t[5])) : 0u);
        dccore::PadRaw p;
        p.lx = uint8_t(int(t[0]));
        p.ly = uint8_t(int(t[1]));
        p.rx = uint8_t(int(t[2]));
        p.ry = uint8_t(int(t[3]));
        p.buttons = uint32_t(int(t[4]));
        script_.push_back(p);
    }
}

void DcTownWalk::teleport(double x, double y, double z) {
    walker_->place_at(float(x), float(z), float(y));
    apply_state();
}

bool DcTownWalk::start_event(int id) {
    if (!rt_) return false;
    // the event starts with the hero where the player stands
    dccore::EvActor &h = rt_->actor(-1);
    h.pos = {walker_->player.pos[0], walker_->player.pos[1], walker_->player.pos[2]};
    h.rot = {0, walker_->player.yaw, 0};
    h.loaded = true;
    rt_->interior_flag = town_->interior ? 1 : 0;
    rt_->pad_down = last_ok_down_ ? 0x40 : 0;   // (events that poll the button in their first frame: the signboards)
    rt_->pad_on = last_ok_on_ ? 0x40 : 0;
    rt_->pad_up = 0;
    const bool running = rt_->start(id);
    const int code = rt_->result_code();
    if (running) {
        mode_ = ModeEvent;
        return true;
    }
    if (code == 9) {   // the talk selector: it fills the list and ends at once
        mode_ = ModeTalk;
        return true;
    }
    if (code > 0) { finish_event(); return true; }
    finish_event();
    return false;
}

void DcTownWalk::finish_event() {
    const int code = rt_->result_code();
    const dccore::EvNav nav = rt_->nav();
    // the hero stays where the script left him; the follow camera is rebuilt behind him
    const dccore::EvActor &h = rt_->actor(-1);
    walker_->place_at(h.pos.x, h.pos.z, h.pos.y + 20.0f);
    walker_->player.yaw = h.rot.y;
    const dccore::EvCamera &c = rt_->camera;
    if (c.reset_mode >= 0) {
        walker_->camera.angle = walker_->camera.angle_cur = h.rot.y + c.reset_angle;
        dccore::camera_step(walker_->camera, 1);
        for (int i = 0; i < 3; ++i) { walker_->camera.pos[i] = walker_->camera.next_pos[i]; walker_->camera.ref[i] = walker_->camera.next_ref[i]; }
    }
    const bool keep_hero = fish_active_ || code == 0xb;   // (the hero with his rod stays while fishing goes on)
    for (auto it = actor_nodes_.begin(); it != actor_nodes_.end();) {   // event actors go away; the standing villagers come back
        if (keep_hero && it->first == -1) { ++it; continue; }
        remove_child(it->second);
        it->second->queue_free();
        it = actor_nodes_.erase(it);
    }
    for (int k = 0; k < 4; ++k)
        if (scene_nodes_[k]) { remove_child(scene_nodes_[k]); scene_nodes_[k]->queue_free(); scene_nodes_[k] = nullptr; }
    if (player_ && !keep_hero) player_->set_visible(true);
    actor_serial_.clear();
    actor_generation_.clear();
    mode_ = ModeWalk;
    if (code == 0xb && nav.kind == dccore::EvNav::Fishing) {   // _GOTO_FISHING: the fishing mode takes over
        if (rt_->next_event() >= 0) pending_event_ = rt_->next_event();
        fish_enter();
        return;
    }
    if (fish_active_) {   // the rod was put away
        fish_leave();
        if (actor_nodes_.count(-1)) { remove_child(actor_nodes_[-1]); actor_nodes_[-1]->queue_free(); actor_nodes_.erase(-1); }
        if (player_) player_->set_visible(true);
    }
    if (code == 4 || code == 5 || code == 7 || code == 8 || code == 0xb) {
        Dictionary d;
        d["kind"] = int(nav.kind);
        d["map"] = nav.map;
        d["name"] = String(nav.name.c_str());
        d["event"] = nav.event;
        d["floor"] = nav.floor;
        d["code"] = code;
        pending_nav_.push_back(d);
    }
    if (rt_->next_event() >= 0) pending_event_ = rt_->next_event();
}

void DcTownWalk::begin_talk(int person) {
    talk_person_ = person;
    talk_select_ = 0;
    talk_phase_ = 0;
    if (!rt_) return;
    rt_->talk_npc_info = villager_info_of(person);
    rt_->talk_list.clear();
    rt_->talk_kind = -1;
    rt_->talk_header = -1;
    rt_->interior_flag = town_->interior ? 1 : 0;
    const bool running = rt_->start(256);
    (void)running;
    if (rt_->talk_list.empty()) { mode_ = ModeWalk; return; }
    mode_ = ModeTalk;
    dccore::MesWindow &w = rt_->windows[0];
    w.bank = rt_->has_talk_bank ? &rt_->talk_bank : nullptr;
    w.system_bank = &rt_->system_bank;
    if (rt_->talk_kind >= 0) { w.make(rt_->talk_header, 0, {}, {}); w.cursor = 0; talk_phase_ = 0; }
    else { w.make(rt_->talk_list[0], 0, {}, {}); w.cursor = -1; talk_phase_ = 1; }
    w.tail = 1;
}

void DcTownWalk::step_talk(bool ok_down, uint32_t menu_pressed) {
    dccore::MesWindow &w = rt_->windows[0];
    w.step();
    // the hero and the villager face each other
    if (talk_person_ >= 0 && size_t(talk_person_) < town_->people.size()) {
        const dccore::TownPerson &p = town_->people[size_t(talk_person_)];
        const float dx = p.pos[0] - walker_->player.pos[0], dz = p.pos[2] - walker_->player.pos[2];
        walker_->player.yaw = dccore::angle_interpolate(walker_->player.yaw, std::atan2(dx, dz), 0.1f, 0);
        if (talk_person_ < int(villager_nodes_.size()) && villager_nodes_[size_t(talk_person_)])
            villager_nodes_[size_t(talk_person_)]->set_rotation(Vector3(0, dccore::angle_interpolate(villager_nodes_[size_t(talk_person_)]->get_rotation().y, std::atan2(-dx, -dz), 0.1f, 0), 0));
    }
    if (talk_phase_ == 0) {   // the choice header: up / down move the cursor, OK takes it
        const int lines = std::max(1, int(std::count(w.page_text().begin(), w.page_text().end(), '\n')) + 1);
        if (menu_pressed & 0x1000) talk_select_ = (talk_select_ + lines - 1) % lines;
        if (menu_pressed & 0x4000) talk_select_ = (talk_select_ + 1) % lines;
        w.cursor = talk_select_;
        if (ok_down) {
            if (w.state() == dccore::MesWindow::Printing) { w.speed = 0; }
            else if (w.state() == dccore::MesWindow::Complete) {
                const int target = size_t(talk_select_) < rt_->talk_list.size() ? rt_->talk_list[size_t(talk_select_)] : -1;
                if (rt_->talk_kind == 0 && talk_select_ == 3 && env_.save && talk_person_ >= 0) {   // asking about the wish marks the villager (GrdNPCData flag 2)
                    const int npc = villager_info_of(talk_person_);
                    if (uint8_t *b = env_.save->grd_data(env_.map_index); b && npc >= 0 && npc < 20) { uint32_t f; std::memcpy(&f, b + 0xac8 + 0xc * size_t(npc), 4); f |= 2; std::memcpy(b + 0xac8 + 0xc * size_t(npc), &f, 4); }
                }
                {   // the shop-style lists (EdTalkMode): the first entry (the fourth list: entries 1..4) opens a shop, the counter or an event
                    const int kind = rt_->talk_kind;
                    int shop = -1, charge = 0, event = -1;
                    if ((kind == 1 || kind == 2 || kind == 3) && talk_select_ == 0 && !rt_->talk_list.empty()) {
                        shop = rt_->talk_list[0];
                        if (kind == 3) event = rt_->talk_list[0];
                        charge = kind == 2 ? 1 : 0;
                    } else if (kind == 4 && talk_select_ >= 1 && talk_select_ <= 4 && size_t(talk_select_) < rt_->talk_list.size()) {
                        shop = rt_->talk_list[size_t(talk_select_)];
                        charge = talk_select_ == 2 ? 1 : 0;
                    }
                    if (shop >= 100) { shop -= 100; charge = 1; }
                    if (event >= 0) {
                        w.close(); w.step();
                        mode_ = ModeWalk;
                        pending_event_ = event;
                        return;
                    }
                    if (shop >= 0) {
                        w.close(); w.step();
                        shop_request_["shop"] = shop;
                        shop_request_["charge"] = charge;
                        mode_ = ModeShop;
                        return;
                    }
                }
                w.close();
                w.step();
                if (target >= 0) { w.make(target, 0, {}, {}); w.cursor = -1; w.tail = 1; talk_phase_ = 1; }
                else mode_ = ModeWalk;
            }
        }
    } else {
        if (ok_down) {
            const int st = w.state();
            if (st == dccore::MesWindow::PageWait) w.next_page();
            else if (st == dccore::MesWindow::Complete) { w.close(); mode_ = ModeWalk; }
            else if (st == dccore::MesWindow::Printing) w.speed = 0;
        }
    }
}

bool DcTownWalk::event_active(const dccore::TownEvent &e) const {
    if (e.flag > 0 && env_.save && env_.save->map_flag(env_.map_index, e.flag)) return false;
    if (!(e.t_start <= 0.0f && e.t_end >= 24.0f) && env_.save) {   // a time window (clock hours, wrapping over midnight)
        const float h = dccore::EventRuntime::invert_time(env_.save->now_time());
        const bool inside = e.t_start <= e.t_end ? (h >= e.t_start && h < e.t_end) : (h >= e.t_start || h < e.t_end);
        if (!inside) return false;
    }
    return true;
}

void DcTownWalk::step_walk(const dccore::PadRaw &pad, bool ok_down, uint32_t menu_pressed) {
    (void)menu_pressed;
    walker_->tick(pad);
    if (town_->interior) {   // the fixed cameras of the room look at the hero from where the camera zone stands
        float cp[3];
        if (interior_camera_.update(*town_, walker_->player.pos, cp)) {
            dccore::CameraState &c = walker_->camera;
            c.follow_on = false;
            const float ref[3] = {walker_->player.pos[0], walker_->player.pos[1] + 17.0f, walker_->player.pos[2]};
            for (int k = 0; k < 3; ++k) { c.pos[k] = c.next_pos[k] = cp[k]; c.ref[k] = c.next_ref[k] = ref[k]; }
        }
    }
    const float *pp = walker_->player.pos;
    // trigger points
    near_event_ = -1;
    float best = 1e30f;
    for (size_t i = 0; i < town_->events.size(); ++i) {
        const dccore::TownEvent &e = town_->events[i];
        if (!event_active(e)) continue;
        const float dx = pp[0] - e.pos[0], dy = pp[1] - e.pos[1], dz = pp[2] - e.pos[2];
        float d;
        if (e.kind == dccore::TownEvent::Item) {
            if (dy < -1.0f || dy > 20.0f) continue;
            d = std::sqrt(dx * dx + dz * dz);
        } else {
            d = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        if (d < e.extent && d < best) { best = d; near_event_ = int(i); }
    }
    // a villager in reach
    near_person_ = -1;
    float bp = 1e30f;
    for (size_t i = 0; i < town_->people.size(); ++i) {
        if (i >= villager_visible_.size() || !villager_visible_[i]) continue;
        const dccore::TownPerson &p = town_->people[i];
        const float dx = pp[0] - p.pos[0], dz = pp[2] - p.pos[2];
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d < 12.0f * 1.2f + 5.0f && d < bp) { bp = d; near_person_ = int(i); }
    }
    if (near_event_ >= 0) {
        const dccore::TownEvent &e = town_->events[size_t(near_event_)];
        if (e.kind == dccore::TownEvent::Event && e.event_no > 0 && rt_) { pending_event_ = e.event_no; return; }
        if (ok_down && e.kind == dccore::TownEvent::Item && env_.status && env_.items && env_.rng) {   // EdGetItem: into the bag, the point is used up
            if (dccore::check_item_get(*env_.status, e.item) == 0) {
                dccore::get_item(*env_.status, *env_.items, e.item, *env_.rng);
                if (rt_) rt_->item_message(e.item);
                if (env_.save && e.flag > 0) env_.save->set_map_flag(env_.map_index, e.flag, true);
            }
            return;
        }
        if (ok_down) {
            if (e.kind == dccore::TownEvent::MapJump) {
                if (e.event_no > 0 && rt_) { pending_event_ = e.event_no; return; }
                Dictionary d;
                d["kind"] = int(town_->interior ? dccore::EvNav::Outside : dccore::EvNav::Interior);
                d["name"] = String(e.map.c_str());
                d["index"] = near_event_;
                d["map"] = 0;
                d["event"] = -1;
                d["floor"] = -1;
                d["code"] = 4;
                d["jump_id"] = e.jump_id;
                pending_nav_.push_back(d);
                return;
            }
        }
    }
    if (ok_down && near_person_ >= 0 && rt_) begin_talk(near_person_);
}

void DcTownWalk::step_event(const dccore::PadRaw &pad, uint32_t menu_bits) {
    rt_->pad_on = 0;
    if (pad.buttons & dccore::kPadOK) rt_->pad_on |= 0x40;
    if (pad.buttons & dccore::kPadNO) rt_->pad_on |= 0x20;
    rt_->pad_on |= menu_bits;
    if (rt_->eb.active()) {   // CGamePad::UpDate: while the EB duel runs the left stick is the d-pad too (threshold 80)
        const int sx = int(pad.lx) - 128, sy = int(pad.ly) - 128;
        if (sx > 80) rt_->pad_on |= 0x2000;
        if (sx < -80) rt_->pad_on |= 0x8000;
        if (sy > 80) rt_->pad_on |= 0x4000;
        if (sy < -80) rt_->pad_on |= 0x1000;
    }
    if (rt_->eb.active() && eb_autoplay_ >= 0) {   // (tests: a perfect player, every key pressed that many ticks after its line)
        rt_->pad_on = 0;
        for (const dccore::EbKey &k : rt_->eb.keys())
            if (rt_->eb.count() - k.tick == eb_autoplay_ || (k.mask > 0 && rt_->eb.count() - k.tick == eb_autoplay_ + 1 && (k.mask & (k.mask - 1)) != 0)) rt_->pad_on |= k.mask;
    }
    rt_->pad_down = rt_->pad_on & ~prev_menu_;
    prev_menu_ = rt_->pad_on;
    rt_->stick[0] = dccore::pad_axis(pad.lx);
    rt_->stick[1] = dccore::pad_axis(pad.ly);
    const bool alive = rt_->step();
    if (!alive) finish_event();
}

void DcTownWalk::step_one() {
    dccore::PadRaw pad;
    uint32_t menu_bits = 0;
    if (size_t(tick_count_) < script_.size()) { pad = script_[size_t(tick_count_)]; menu_bits = script_bits_[size_t(tick_count_)]; }
    else if (use_devices_) { pad = read_devices(menu_bits); dc_pad::record(pad.lx, pad.ly, pad.rx, pad.ry, pad.buttons, menu_bits); }   // (portkit replay: PK_RECORD_PAD)
    const bool ok_now = (pad.buttons & dccore::kPadOK) != 0, ok_down = ok_now && !prev_ok_;
    prev_ok_ = ok_now;
    last_ok_down_ = ok_down;
    last_ok_on_ = ok_now;
    const uint32_t menu_pressed = menu_bits & ~prev_menu_;
    ++tick_count_;
    if (pending_event_ >= 0 && mode_ == ModeWalk) {
        const int id = pending_event_;
        pending_event_ = -1;
        start_event(id);
    }
    if (rt_ && mode_ != ModeEvent) rt_->step_fade();
    if (mode_ == ModeWalk && can_edit() && (menu_pressed & 0x100) && pending_event_ < 0 && !(rt_ && rt_->running())) {   // Select: to the edit mode
        edit_enter();
        prev_menu_ = menu_bits;
        return;
    }
    if (mode_ == ModeEdit) {
        step_edit(pad, menu_bits);
        prev_menu_ = menu_bits;
        return;
    }
    if (mode_ == ModeWalk) {
        // the clock runs while the player walks (EditLoop: 0.00111 per tick, halved by the default option, not in maps with TIME_STOP); a new part of the day runs event 0x84 in the first four towns
        if (env_.save && !town_->info.time_stop && !town_->interior) {
            const int before = rt_ ? rt_->time_bucket() : 0;
            env_.save->add_now_time(env_.save->get<int32_t>(0x0c) == 0 ? 0.0005566667f : 0.0011133333f);   // option TIME PROGRES: the default (normal) runs at half the full rate
            if (rt_ && rt_->time_bucket() != before && env_.map_index >= 0 && env_.map_index <= 3 && rt_->running() == false && pending_event_ < 0 && rt_->has_function(132)) {
                for (size_t i = 0; i < villager_visible_.size(); ++i) villager_visible_[i] = villager_present_now(town_->people[i].villager);
                init_roamers();
                pending_event_ = 132;
            }
        }
        step_roamers();
        step_walk(pad, ok_down, menu_pressed);
        if (geo_ && !town_->interior && rt_ && tick_count_ > 60 && pending_event_ < 0 && mode_ == ModeWalk) {   // a finished house whose event plays when the hero stands on it (MoveChara)
            const int part = geo_->pending_completion_under(walker_->player.pos);
            if (part >= 0 && rt_->has_function(200 + part)) pending_event_ = 200 + part;
        }
        prev_menu_ = menu_bits;
    } else if (mode_ == ModeEvent) {
        step_event(pad, menu_bits);
    } else if (mode_ == ModeFish) {
        step_fish(pad, ok_down, menu_pressed);
        prev_menu_ = menu_bits;
    } else if (mode_ == ModeShop) {
        prev_menu_ = menu_bits;
    } else {
        step_talk(ok_down, menu_pressed);
        prev_menu_ = menu_bits;
    }
}

void DcTownWalk::step_ticks(int n) {
    for (int i = 0; i < n; ++i) step_one();
    apply_state();
}

void DcTownWalk::_physics_process(double delta) {
    accumulator_ += delta * 60.0;
    int n = int(accumulator_);
    accumulator_ -= n;
    if (n > 4) n = 4;
    if (n > 0) step_ticks(n);
}

Array DcTownWalk::take_nav() {
    Array out = pending_nav_;
    pending_nav_ = Array();
    return out;
}

Vector3 DcTownWalk::get_player_position() const {
    const auto &p = walker_->player.pos;
    return Vector3(p[0], p[1], p[2]);
}

Vector2 DcTownWalk::screen_of(int slot) const {
    if (!camera_) return Vector2();
    Vector3 p;
    float h = 20.0f;
    if (mode_ == ModeEvent && rt_) { const dccore::EvActor &a = rt_->actor(slot); p = Vector3(a.pos.x, a.pos.y, a.pos.z); h = a.body[1]; }
    else if (slot == -1) { p = Vector3(walker_->player.pos[0], walker_->player.pos[1], walker_->player.pos[2]); }
    else if (talk_person_ >= 0 && size_t(talk_person_) < town_->people.size()) { const auto &t = town_->people[size_t(talk_person_)]; p = Vector3(t.pos[0], t.pos[1], t.pos[2]); }
    p.y += h;
    if (camera_->is_position_behind(p)) return Vector2(-1000, -1000);
    return camera_->unproject_position(p);
}

void DcTownWalk::apply_actors() {
    if (mode_ != ModeEvent || !rt_) {
        for (size_t i = 0; i < villager_nodes_.size(); ++i)
            if (villager_nodes_[i]) villager_nodes_[i]->set_visible(i < villager_visible_.size() && villager_visible_[i] && mode_ != ModeEvent);
        return;
    }
    for (DcCharacter *v : villager_nodes_) if (v) v->set_visible(false);
    for (auto &kv : actor_nodes_) {
        const int slot = kv.first;
        DcCharacter *node = kv.second;
        const dccore::EvActor &a = rt_->actor(slot);
        node->set_visible(a.draw);
        node->set_position(Vector3(a.pos.x, a.pos.y, a.pos.z));
        node->set_rotation(Vector3(a.rot.x, a.rot.y, a.rot.z));
        node->set_scale(Vector3(a.scale.x, a.scale.y, a.scale.z));
        if (actor_serial_[slot] != a.motion_serial) {
            actor_serial_[slot] = a.motion_serial;
            node->play(a.motion, (a.motion_flags & 2) == 0);
            const Dictionary mi = node->get_motion_info(a.motion);
            const double table = mi.has("speed") ? double(mi["speed"]) : 1.0;
            node->set_speed_scale(a.motion_speed > 0.0f && table > 0.0 ? double(a.motion_speed) / table : 1.0);
        }
    }
    if (player_ && !actor_nodes_.count(-1)) {
        const dccore::EvActor &h = rt_->actor(-1);
        player_->set_visible(h.draw);
        player_->set_position(Vector3(h.pos.x, h.pos.y, h.pos.z));
        player_->set_rotation(Vector3(0, h.rot.y, 0));
        if (actor_serial_[-1] != h.motion_serial) {
            actor_serial_[-1] = h.motion_serial;
            player_->play(h.motion, (h.motion_flags & 2) == 0);
        }
    }
}

void DcTownWalk::apply_state() {
    const dccore::PlayerState &p = walker_->player;
    const dccore::CameraState *cam = &walker_->camera;
    if (mode_ == ModeEvent && rt_) cam = &rt_->camera.cs;
    else if (mode_ == ModeEdit) cam = &edit_cam_;
    if (player_ && mode_ != ModeEvent && mode_ != ModeFish) {
        player_->set_visible(true);
        player_->set_position(Vector3(p.pos[0], p.pos[1], p.pos[2]));
        player_->set_rotation(Vector3(0, p.yaw, 0));
        if (p.motion != last_motion_) {
            player_->play(p.motion, true);
            last_motion_ = p.motion;
        }
        const Dictionary info = player_->get_motion_info(p.motion);
        const double table = info.has("speed") ? double(info["speed"]) : 1.0;
        double wanted = table;
        if (p.motion == 0) wanted = 0.05;
        else if (p.motion == 1) wanted = p.motion_speed;
        player_->set_speed_scale(table > 0.0 ? wanted / table : 1.0);
    }
    apply_actors();
    apply_handles();
    step_scenes();
    Vector3 cam_pos = free_cam_ ? free_pos_ : Vector3(cam->pos[0], cam->pos[1], cam->pos[2]);
    Vector3 ref = free_cam_ ? free_ref_ : Vector3(cam->ref[0], cam->ref[1], cam->ref[2]);
    event_fov_ = 0;
    if (mode_ == ModeFish) {
        fish_apply();
        if (!free_cam_) { cam_pos = fish_camera_pos(); ref = fish_camera_ref(); }
    }
    if (mode_ == ModeEvent && rt_ && !free_cam_)
        for (int k = 0; k < 4; ++k) {
            if (!rt_->scenes[size_t(k)].camera_sync || !scene_nodes_[k]) continue;
            const Dictionary c = scene_nodes_[k]->sample_camera(scene_time_[k]);
            if (!c.has("pos")) continue;
            const dccore::EvScene &sc = rt_->scenes[size_t(k)];
            const Basis b(Vector3(0, 1, 0), sc.rot.y);
            cam_pos = b.xform(Vector3(c["pos"])) + Vector3(sc.pos.x, sc.pos.y, sc.pos.z);
            ref = b.xform(Vector3(c["ref"])) + Vector3(sc.pos.x, sc.pos.y, sc.pos.z);
            event_fov_ = float(double(c["fov"]));
            break;
        }
    if (cam_pos.distance_squared_to(ref) > 1e-6) {
        Transform3D t;
        t.origin = cam_pos;
        camera_->set_transform(t.looking_at(ref, Vector3(0, 1, 0)));
    } else {
        camera_->set_position(cam_pos);
    }
    if (event_fov_ > 1.0f) {
        camera_->set_fov(event_fov_);
    } else if (mode_ == ModeEvent && rt_ && rt_->camera.projection > 0.0f) {   // the game's projection value is a focal length for a 448 line screen
        camera_->set_fov(2.0 * std::atan(224.0 / double(rt_->camera.projection)) * 180.0 / 3.14159265358979);
    } else if (town_->interior) {
        camera_->set_fov(2.0 * std::atan(224.0 / double(town_->info.projection)) * 180.0 / 3.14159265358979);
    } else {
        camera_->set_fov(31.3);
    }
    if (scene_) { scene_->update_sky(get_clock(), cam_pos); scene_->set_clock(get_clock()); }
}

Dictionary DcTownWalk::get_eb_ui() const { return eb_ui_dictionary(rt_ ? &rt_->eb : nullptr); }

Dictionary DcTownWalk::get_ui() {
    Dictionary ui;
    ui["mode"] = int(mode_);
    if (fish_active_) {
        Dictionary fd;
        fd["state"] = fish_state_;
        fd["bait"] = fish_bait_item_;
        fd["text"] = String::utf8(fish_catch_text_.c_str());
        if (fish_sim_) {
            fd["float"] = fish_float_;
            fd["tip"] = fish_rod_tip();
            fd["yaw"] = fish_yaw_;
            fd["hero"] = fish_hero_;
            fd["area"] = Vector4(rt_->fishing.area[0], rt_->fishing.area[1], rt_->fishing.area[2], rt_->fishing.area[3]);
            Array fa;
            for (int i = 0; i < fish_sim_->fish_num; ++i) fa.push_back(Vector3(fish_sim_->fish[size_t(i)].pos[0], fish_sim_->fish[size_t(i)].pos[1], fish_sim_->fish[size_t(i)].pos[2]));
            fd["fish"] = fa;
            Array bs;
            for (int i = 0; i < fish_sim_->fish_num; ++i) bs.push_back(fish_sim_->fish[size_t(i)].bite * 100 + fish_sim_->fish[size_t(i)].kind);
            fd["bites"] = bs;
            fd["sim_bait"] = fish_sim_->bait;
        }
        ui["fish"] = fd;
    }
    Array wins;
    if (rt_) {
        for (size_t i = 0; i < rt_->windows.size(); ++i) {
            const dccore::MesWindow &w = rt_->windows[i];
            if (w.state() == dccore::MesWindow::Closed) continue;
            Dictionary d;
            d["index"] = int(i);
            d["state"] = w.state();
            d["text"] = String::utf8(w.visible_text().c_str());
            d["full"] = String::utf8(w.page_text().c_str());
            d["cursor"] = w.cursor;
            d["pos"] = w.pos_mode;
            d["tail"] = w.tail;
            d["style"] = w.fukidashi;
            d["openness"] = w.openness;
            d["okuri"] = w.okuri && (w.state() == dccore::MesWindow::PageWait);
            d["a"] = screen_of(w.autoset_a >= -1 ? w.autoset_a : -1);
            d["b"] = screen_of(w.autoset_b >= -1 ? w.autoset_b : -1);
            d["has_a"] = w.autoset_a >= -1 && !w.autoset_rect;
            wins.push_back(d);
        }
        Dictionary fade;
        fade["alpha"] = rt_->fade.alpha / 128.0f;
        fade["color"] = Color(rt_->fade.r / 255.0f, rt_->fade.g / 255.0f, rt_->fade.b / 255.0f);
        ui["fade"] = fade;
        ui["system_message"] = rt_->system_message.active ? String::utf8(rt_->system_message.text.c_str()) : String();
        ui["exclamation"] = rt_->exclamation;
        ui["day_banner"] = rt_->day_banner;
        // sprites (640 x 448 screen space), the colour filter, the clear colour and the light overrides of the script
        Array sp;
        for (const dccore::EvSprite &s : rt_->draw_sprites) sp.push_back(sprite_dict(s));
        for (const dccore::EvSprite &s : rt_->sprites) if (s.active && s.show) sp.push_back(sprite_dict(s));
        ui["sprites"] = sp;
        if (rt_->filter_on) ui["filter"] = Color(rt_->filter[0] / 255.0f, rt_->filter[1] / 255.0f, rt_->filter[2] / 255.0f, std::min(rt_->filter[3] / 128.0f, 1.0f));
        if (rt_->back_on) ui["back"] = Color(std::max(rt_->back[0], 0.0f) / 255.0f, std::max(rt_->back[1], 0.0f) / 255.0f, std::max(rt_->back[2], 0.0f) / 255.0f);
        if (rt_->ambient_on) ui["ambient"] = Color(rt_->ambient[0] / 128.0f, rt_->ambient[1] / 128.0f, rt_->ambient[2] / 128.0f);
        Array lights;
        for (const dccore::EvLight &l : rt_->lights)
            if (l.on) {
                Dictionary d;
                d["direction"] = Vector3(l.dir[0], l.dir[1], l.dir[2]).normalized();
                d["color"] = Color(l.color[0] / 128.0f, l.color[1] / 128.0f, l.color[2] / 128.0f);
                lights.push_back(d);
            }
        if (lights.size() > 0) ui["lights"] = lights;
    }
    ui["windows"] = wins;
    if (can_edit()) ui["edit"] = get_edit_ui();
    if (mode_ == ModeWalk) {
        if (near_person_ >= 0) ui["prompt"] = String("Talk");
        else if (near_event_ >= 0) ui["prompt"] = String(town_->events[size_t(near_event_)].kind == dccore::TownEvent::MapJump ? "Enter" : "Use");
    }
    return ui;
}

void DcTownWalk::place_hero(double x, double y, double z, double yaw) {
    walker_->place_at(float(x), float(z), float(y));
    walker_->player.yaw = float(yaw);
    walker_->camera.angle = walker_->camera.angle_cur = float(yaw) + 3.14159f;
    dccore::camera_step(walker_->camera, 1);
    for (int i = 0; i < 3; ++i) { walker_->camera.pos[i] = walker_->camera.next_pos[i]; walker_->camera.ref[i] = walker_->camera.next_ref[i]; }
    apply_state();
}

Dictionary DcTownWalk::get_door_out(int event_index) const {
    Dictionary d;
    if (event_index < 0 || size_t(event_index) >= town_->events.size()) return d;
    const dccore::TownEvent &e = town_->events[size_t(event_index)];
    if (e.has_out) {
        d["pos"] = Vector3(e.out_pos[0], e.out_pos[1], e.out_pos[2]);
    } else {   // a fixed step out of the door, in the direction the door faces
        const float s = std::sin(e.yaw), c = std::cos(e.yaw);
        d["pos"] = Vector3(e.pos[0] + s * 25.0f, e.pos[1], e.pos[2] + c * 25.0f);
    }
    d["yaw"] = e.yaw;
    return d;
}

// the frames of map objects the script moved or hid (_HOBJ_* with _SET_OBJ_POS / _SET_OBJ_ROT / _OBJ_DRAW)
void DcTownWalk::apply_handles() {
    if (!rt_ || !scene_) return;
    for (int h = 0; h < 32; ++h) {
        const dccore::EvHandle &hd = rt_->handles[size_t(h)];
        if (hd.kind == 4) {   // a frame of an actor's model (a propeller, a tail): on top of the motion
            auto an = actor_nodes_.find(hd.index);
            if (an == actor_nodes_.end() || !an->second || !hd.moved) continue;
            HandleApplied &ap = handle_applied_[h];
            if (ap.moved && hd.pos.x == ap.pos.x && hd.pos.y == ap.pos.y && hd.pos.z == ap.pos.z && hd.rot.x == ap.rot.x && hd.rot.y == ap.rot.y && hd.rot.z == ap.rot.z) continue;
            an->second->set_joint_override(an->second->find_joint(String(hd.frame.c_str())), Vector3(hd.rot.x, hd.rot.y, hd.rot.z), Vector3(hd.pos.x, hd.pos.y, hd.pos.z), hd.rot_set, hd.pos_set);
            ap.moved = true;
            ap.pos = hd.pos;
            ap.rot = hd.rot;
            continue;
        }
        if (hd.kind != 1 && hd.kind != 2 && hd.kind != 3) continue;
        const std::vector<int> &list = hd.kind == 1 ? fixed_instances_ : placed_instances_;
        int inst = -1;
        if (hd.kind == 3) inst = town_->instances.empty() ? -1 : 0;
        else if (hd.index >= 0 && size_t(hd.index) < list.size()) inst = list[size_t(hd.index)];
        if (inst < 0) continue;
        HandleApplied &ap = handle_applied_[h];
        if (hd.draw != ap.drawn) {
            scene_->set_frame_visible(inst, String(hd.frame.c_str()), hd.draw);
            ap.drawn = hd.draw;
        }
        if (hd.moved && (!ap.moved || hd.pos.x != ap.pos.x || hd.pos.y != ap.pos.y || hd.pos.z != ap.pos.z || hd.rot.x != ap.rot.x || hd.rot.y != ap.rot.y || hd.rot.z != ap.rot.z)) {
            scene_->set_frame_transform(inst, String(hd.frame.c_str()), Vector3(hd.pos.x, hd.pos.y, hd.pos.z), Vector3(hd.rot.x, hd.rot.y, hd.rot.z), hd.pos_set, hd.rot_set);
            ap.moved = true;
            ap.pos = hd.pos;
            ap.rot = hd.rot;
        }
    }
}

// the scene models (cut-scene stages) play their motion once from the start; characters hang on their frames
void DcTownWalk::step_scenes() {
    if (mode_ != ModeEvent || !rt_) return;
    for (int k = 0; k < 4; ++k) {
        DcCharacter *node = scene_nodes_[k];
        if (!node) continue;
        const dccore::EvScene &sc = rt_->scenes[size_t(k)];
        const auto &mot = node->get_character()->motions;
        const double end = mot.empty() ? 0.0 : double(mot[0].end);
        if (end > 0.0 && scene_time_[k] < end) scene_time_[k] = std::min(end, scene_time_[k] + double(scene_step_[k]));
        node->set_playing(false);
        if (node->get_motion() != 0) node->play(0, false);
        node->set_playing(false);
        node->set_time(scene_time_[k]);
        node->set_position(Vector3(sc.pos.x, sc.pos.y, sc.pos.z));
        node->set_rotation(Vector3(0, sc.rot.y, 0));
    }
    // actors attached to a frame of a scene
    for (auto &kv : actor_nodes_) {
        const dccore::EvActor &a = rt_->actor(kv.first);
        if (a.attach_kind != 1 || a.attach_target < 0 || a.attach_target >= 4 || !scene_nodes_[a.attach_target]) continue;
        DcCharacter *sn = scene_nodes_[a.attach_target];
        const int j = sn->find_joint(String(a.attach_frame.c_str()));
        if (j < 0) continue;
        dccore::Mat4 identity{};
        identity[0] = identity[5] = identity[10] = identity[15] = 1.0;
        kv.second->set_transform(sn->get_transform() * sn->attach_transform(j, identity));
    }
    const dccore::EvActor &hero = rt_->actor(-1);   // (the hero, when his own model is the player node)
    if (player_ && !actor_nodes_.count(-1) && hero.attach_kind == 1 && hero.attach_target >= 0 && hero.attach_target < 4 && scene_nodes_[hero.attach_target]) {
        DcCharacter *sn = scene_nodes_[hero.attach_target];
        const int j = sn->find_joint(String(hero.attach_frame.c_str()));
        if (j >= 0) {
            dccore::Mat4 identity{};
            identity[0] = identity[5] = identity[10] = identity[15] = 1.0;
            player_->set_transform(sn->get_transform() * sn->attach_transform(j, identity));
        }
    }
}

Dictionary DcTownWalk::sprite_dict(const dccore::EvSprite &s) {
    Dictionary d;
    auto it = sprite_textures_.find(s.tex);
    d["tex"] = it != sprite_textures_.end() ? Variant(it->second) : Variant();
    d["x"] = s.x;
    d["y"] = s.y;
    d["u"] = s.u; d["v"] = s.v; d["w"] = s.w; d["h"] = s.h;
    d["sw"] = s.sw > 0 ? s.sw : float(s.w);
    d["sh"] = s.sh > 0 ? s.sh : float(s.h);
    d["color"] = Color(s.rgba[0] / 128.0f, s.rgba[1] / 128.0f, s.rgba[2] / 128.0f, s.rgba[3] / 128.0f);
    d["rot"] = s.rot;
    d["rot_cx"] = s.rot_cx; d["rot_cy"] = s.rot_cy;
    d["bg"] = s.background;
    return d;
}

Array DcTownWalk::get_villagers() const {
    Array out;
    for (size_t i = 0; i < town_->people.size(); ++i) {
        Dictionary d;
        d["villager"] = town_->people[i].villager;
        d["pos"] = Vector3(town_->people[i].pos[0], town_->people[i].pos[1], town_->people[i].pos[2]);
        d["visible"] = i < villager_visible_.size() && villager_visible_[i];
        d["roamer"] = i < roam_.size() && roam_[i].roamer;
        d["moving"] = i < roam_.size() && roam_[i].moving;
        out.push_back(d);
    }
    return out;
}

Array DcTownWalk::get_town_events() const {
    Array out;
    for (const dccore::TownEvent &e : town_->events) {
        Dictionary d;
        d["kind"] = int(e.kind);
        d["event_no"] = e.event_no;
        d["pos"] = Vector3(e.pos[0], e.pos[1], e.pos[2]);
        d["extent"] = e.extent;
        d["map"] = String(e.map.c_str());
        out.push_back(d);
    }
    return out;
}

// ---- co-op ------------------------------------------------------------------------------------------------------------------------------------------------

void DcTownWalk::set_hero_model(DcCharacter *model) {
    if (!model) return;
    const bool visible = player_ ? player_->is_visible() : true;
    if (player_) { remove_child(player_); player_->queue_free(); }
    player_ = model;
    add_child(player_);
    player_->set_visible(visible);
    if (hero_color_ >= 0) player_->set_player_color(hero_color_);
    last_motion_ = -1;
}

void DcTownWalk::set_hero_color(int index) {
    hero_color_ = index;
    if (player_) player_->set_player_color(index);
    auto it = actor_nodes_.find(-1);
    if (it != actor_nodes_.end() && it->second) it->second->set_player_color(index);
}

Dictionary DcTownWalk::get_hero_pose() const {
    Dictionary d;
    DcCharacter *node = player_;
    auto it = actor_nodes_.find(-1);
    const bool staged = it != actor_nodes_.end() && it->second;
    if (staged) node = it->second;
    if (!node) return d;
    d["pos"] = node->get_position();
    d["yaw"] = node->get_rotation().y;
    d["motion"] = node->get_motion();
    d["time"] = node->get_time();
    d["mode"] = int(mode_);
    d["visible"] = node->is_visible();
    if (mode_ == ModeFish && fish_sim_) {   // what the others need to draw his line: the float, the rod tip, whether the line is out (co-op puppets)
        Dictionary f;
        f["float"] = fish_float_;
        f["tip"] = fish_rod_tip();
        f["out"] = fish_line_out_ || fish_state_ == 10 || fish_state_ == 11 || fish_state_ == 5 || fish_state_ == 9;
        f["slack"] = fish_line_out_ ? 1.0f : 0.3f;
        d["fish"] = f;
    }
    return d;
}
