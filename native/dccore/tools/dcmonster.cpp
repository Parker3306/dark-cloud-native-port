// Headless monster run: dcmonster <iso> <stem> <seed> <rooms> <map_no> <floor> <ticks> [--player x y z] [--arrange-seed n] [--feed file] [--rotmode m] [--slot k] [--trace]
// --arrange-seed: srand(n) before the monsters are placed (the live rig does that before calling BtArrengeMonstor)
// --feed: one line per tick "px py pz rand_state" (what the live game had at the start of that tick); the player is moved and rand() reseeded before each tick
// Builds the floor, arranges the monsters as the game does, puts the player at the start cell (or --player), and steps the monsters.
// Prints one line per tick and active monster: tick slot state x y z yaw | move_speed | motion id time | script_active
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string>
#include "dccore/character.h"
#include "dccore/disc.h"
#include "dccore/gamemath.h"
#include "dccore/dungeon.h"
#include "dccore/floor.h"
#include "dccore/monster_ai.h"
#include "dccore/pack.h"
#include "dccore/walk.h"
#include "dccore/player.h"

using namespace dccore;

int main(int argc, char** argv) {
    if (argc < 8) {
        std::fprintf(stderr, "usage: dcmonster <iso> <stem> <seed> <rooms> <map_no> <floor> <ticks> [options]\n");
        return 2;
    }
    if (std::getenv("DC_RN")) dccore::gm::round_to_zero() = false;   // experiment: round to nearest instead of toward zero
    try {
        Disc disc{std::filesystem::path(argv[1])};
        const std::string stem = argv[2];
        const int map_no = std::atoi(argv[5]), floor = std::atoi(argv[6]), ticks = std::atoi(argv[7]);
        Bytes cfg = disc.read("dun/" + stem + ".cfg");
        Pack pack(disc.read("dun/mpd_pack/" + stem + ".mpd"));
        Dungeon dun = load_dungeon(std::string(cfg.begin(), cfg.end()), pack);
        GameTables tables = GameTables::load(Elf(disc.read_root("SCUS_971.11")));
        GenState gen;
        gen.tables = &tables;
        FloorLayout layout = build_random_map(uint32_t(std::strtoul(argv[3], nullptr, 0)), std::atoi(argv[4]), map_no, true, &gen);
        MonsterUnit unit;
        unit.load_floor(disc, tables, map_no, floor, false);
        for (int i = 8; i < argc; ++i)
            if (!std::strcmp(argv[i], "--monsters") && i + 1 < argc) {   // id,id,...: use these monsters instead of the floor's
                std::vector<int> ids;
                std::stringstream ss(argv[i + 1]);
                std::string tok;
                while (std::getline(ss, tok, ',')) ids.push_back(std::atoi(tok.c_str()));
                unit.load_ids(disc, tables, ids);
            }
        GameRand rng;
        rng.seed(layout.rand_state);
        for (int i = 8; i < argc; ++i)
            if (!std::strcmp(argv[i], "--arrange-seed") && i + 1 < argc) rng.seed(uint32_t(std::strtoul(argv[i + 1], nullptr, 0)));
        std::vector<std::string> alog;
        for (int i = 8; i < argc; ++i)
            if (!std::strcmp(argv[i], "--arrange-log")) unit.arrange_log = &alog;
        // the world has to exist before the monsters are put down: each one runs its init script (and may use rand()) as it is placed
        Walker walker(&dun, &layout);
        walker.place_at_start();
        MonsterWorld world(&dun, &layout, &unit, &rng);
        world.set_monster_shape(parse_collision_mds(disc.read("dun/etc/atrcol.mds")));
        world.player.pos[0] = walker.player.pos[0]; world.player.pos[1] = walker.player.pos[1]; world.player.pos[2] = walker.player.pos[2];
        unit.arrange(layout, tables, map_no, rng, 15, -1, [&](int slot) {
            world.spawn(slot);
            for (int k = 0; k < 3; ++k) unit.slots[size_t(slot)].pos[k] = world.actors[size_t(slot)].pos[k];
        });
        for (const std::string& l : alog) std::printf("%s\n", l.c_str());
        int only = -1;
        std::string init_file, player_file;   // lines "slot height floor_y": memory a slot still holds from an earlier monster
        std::vector<std::array<double, 5>> feed;
        struct Inject { int tick; float pos[3]; float radius; int damage, flags, owner, sub, target, kind, status, life; };
        std::vector<Inject> injections;
        world.floor_map = map_no;
        ItemDb item_db = ItemDb::load(Elf(disc.read_root("SCUS_971.11")));
        world.item_db = &item_db;
        for (int i = 8; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--player") && i + 3 < argc) { for (int k = 0; k < 3; ++k) world.player.pos[k] = float(std::atof(argv[i + 1 + k])); i += 3; }
            else if (!std::strcmp(argv[i], "--arrange-seed") && i + 1 < argc) ++i;
            else if (!std::strcmp(argv[i], "--feed") && i + 1 < argc) {
                std::ifstream f(argv[++i]);
                std::array<double, 5> v{};
                while (f >> v[0] >> v[1] >> v[2] >> v[3] >> v[4]) feed.push_back(v);
            }
            else if (!std::strcmp(argv[i], "--init") && i + 1 < argc) init_file = argv[++i];
            else if (!std::strcmp(argv[i], "--sim-player") && i + 1 < argc) player_file = argv[++i];   // simulate the player too, from this state file
            else if (!std::strcmp(argv[i], "--status") && i + 1 < argc) {   // raw UserStatus (tools/dc/snapshot_status.py)
                std::ifstream f(argv[++i], std::ios::binary);
                f.read(reinterpret_cast<char*>(world.status.raw.data()), std::streamsize(world.status.raw.size()));
            }
            else if (!std::strcmp(argv[i], "--inject") && i + 1 < argc) {   // tick,x,y,z,radius,damage,flags,owner,sub,target,kind,status,life
                Inject in{};
                if (std::sscanf(argv[++i], "%d,%f,%f,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d", &in.tick, &in.pos[0], &in.pos[1], &in.pos[2], &in.radius, &in.damage, &in.flags, &in.owner, &in.sub, &in.target, &in.kind, &in.status, &in.life) == 13)
                    injections.push_back(in);
            }
            else if (!std::strcmp(argv[i], "--rotmode") && i + 1 < argc) world.rotation_mode = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--slot") && i + 1 < argc) only = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--monster-at") && i + 1 < argc) { int sl; float x, y, z; if (std::sscanf(argv[++i], "%d,%f,%f,%f", &sl, &x, &y, &z) == 4 && sl >= 0 && sl < MonsterUnit::kSlots) { world.actors[size_t(sl)].pos[0] = x; world.actors[size_t(sl)].pos[1] = y; world.actors[size_t(sl)].pos[2] = z; } }
            else if (!std::strcmp(argv[i], "--trace")) world.trace = true;
            else if (!std::strcmp(argv[i], "--extlog")) world.trace_ext = true;
        }
        world.log.clear();
        if (!init_file.empty()) {
            std::ifstream f(init_file);
            int slot;
            float h, fy;
            while (f >> slot >> h >> fy)
                if (slot >= 0 && slot < MonsterUnit::kSlots) { world.actors[size_t(slot)].height = h; world.actors[size_t(slot)].floor_y = fy; }
        }
        world.log.clear();
        for (int s = 0; s < MonsterUnit::kSlots; ++s) {
            const MonsterActor& a = world.actors[size_t(s)];
            if (a.slot < 0) continue;
            std::printf("slot %d base %d %s hp %d radius %.2f at %.3f %.3f %.3f\n", s, a.base, unit.bases[size_t(a.base)].entry.stem().c_str(), a.hp, a.body_radius, a.pos[0], a.pos[1], a.pos[2]);
            std::printf("  motions %zu:", a.table.size());
            for (size_t i = 0; i < a.table.size() && i < 4; ++i) std::printf(" [%d %d %.3f]", a.table[i].start, a.table[i].end, a.table[i].speed);
            std::printf("\n");
        }
        std::unique_ptr<Walker> pw;
        std::unique_ptr<PlayerKit> kit;
        std::vector<SetLog> set_log;
        int shown_weapon = -1;
        Bytes dun_bin_copy;
        auto refresh_weapon = [&]() {   // the weapon in the record changed (it broke): the weapon model and its hit points change with it (EquipWeaponFrame)
            if (!pw || !kit) return;
            const int ch = world.status.character();
            const int id = int(world.status.weapon(ch).id());
            if (shown_weapon == -1) { shown_weapon = id; return; }
            if (id == shown_weapon) return;
            shown_weapon = id;
            if (dun_bin_copy.empty()) dun_bin_copy = disc.read_root("DUN.BIN");
            auto k2 = std::make_unique<PlayerKit>(load_player_kit(disc, dun_bin_copy, ch, id, world.status.weapon(ch).element_choice()));
            k2->model = kit->model;
            kit = std::move(k2);
            pw->setup_weapon(kit->model.get(), kit->tip, kit->tip2, kit->weapon_model.get());
        };
        std::vector<int> presses;
        int hold = 3;
        int no_floor = 0;
        std::vector<std::array<int, 4>> sticks;   // tick range and raw stick bytes
        std::vector<std::array<int, 3>> buttons;  // tick range and a pad mask held in it
        for (int i = 8; i < argc; ++i) {
            if (!std::strcmp(argv[i], "--press") && i + 1 < argc) { std::stringstream ss(argv[++i]); std::string tok; while (std::getline(ss, tok, ',')) presses.push_back(std::atoi(tok.c_str())); }
            else if (!std::strcmp(argv[i], "--hold") && i + 1 < argc) hold = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--btn") && i + 1 < argc) { std::stringstream ss(argv[++i]); std::string tok; while (std::getline(ss, tok, ',')) { int a, b; unsigned m; if (std::sscanf(tok.c_str(), "%d-%d:%i", &a, &b, &m) == 3) buttons.push_back({a, b, int(m)}); } }
            else if (!std::strcmp(argv[i], "--no-floor") && i + 1 < argc) no_floor = std::atoi(argv[++i]);
            else if (!std::strcmp(argv[i], "--stick") && i + 1 < argc) { std::stringstream ss(argv[++i]); std::string tok; while (std::getline(ss, tok, ',')) { int a, b, x, y; if (std::sscanf(tok.c_str(), "%d-%d:%d:%d", &a, &b, &x, &y) == 4) sticks.push_back({a, b, x, y}); } }
        }
        if (!player_file.empty()) {
            pw = std::make_unique<Walker>(&dun, &layout);
            pw->place_at_start();
            pw->status = &world.status;
            pw->pool = &world.pool;
            pw->no_floor_ticks = no_floor;
            pw->rng = &rng;
            {   // the character's motion table, model (the hand that holds the weapon) and the weapon's hit points
                const int ch = world.status.character();
                kit = std::make_unique<PlayerKit>(load_player_kit(disc, disc.read_root("DUN.BIN"), ch, world.status.weapon(ch).id(), world.status.weapon(ch).element_choice()));
                pw->motion_table = kit->motions;
                pw->setup_weapon(kit->model.get(), kit->tip, kit->tip2, kit->weapon_model.get());
                if (kit->has_main_fx) world.load_player_fx(kit->main_fx, kit->main_fx_model, kit->main_fx_row);
                pw->main_shot = [&](const Walker::MainShot& m) -> int { return world.fire_player_shot(m.pos, m.target, m.owner_kind, m.sub_id, m.damage, m.wait, m.force_phase, m.set_attribute, m.set_damage, m.loop, m.random_rate, m.no_sound, m.attach); };
                pw->end_main_shot = [&]() { world.end_player_shot(); };
                pw->off_main_effect = [&](int k) { world.off_player_effect(k); };
                pw->lock = &world.lock;
                pw->monster_pos = [&](int m, float out[3]) { if (m < 0 || m >= MonsterUnit::kSlots) return false; for (int k = 0; k < 3; ++k) out[k] = world.actors[size_t(m)].pos[k]; return true; };
                pw->shift_target = [&](int s) { return world.set_near_target(s, false); };
                pw->osmond_type = kit->osmond_type;
                pw->fire_mg = [&](const float p[3], const float v[3], int dmg) { if (std::getenv("DC_DBG")) std::printf("mg %.4f %.4f %.4f vel %.4f %.4f %.4f dmg %d\n", p[0], p[1], p[2], v[0], v[1], v[2], dmg); world.fire_mg(p, v, dmg); };
                pw->firebar_init = [&](const float p[3], const float dir[3]) { world.firebar_init(p, dir); };
                pw->firebar_set = [&](const float p[3], const float dir[3], int dmg, int el) { world.firebar_set(p, dir, dmg, el); };
                pw->firebar_reset = [&]() { world.firebar_reset(); };
                pw->fire_bullet = [&](const Walker::Bullet& b) { world.fire_sling(b.pos, b.vel, b.damage); };
                pw->sword_dmg = [&](float p) { world.sword_dmg_check(p, 0); };
                pw->pool->log = &set_log;
            }
            pw->monster_collide = [&](const float pos[3], float vel[3], int ch) { world.move_check_player(pos, vel, ch, false); };
            pw->nearest_monster = [&]() {
                float best = 10000.0f;
                for (const MonsterActor& a : world.actors)
                    if (a.slot >= 0 && a.state != -1 && a.active != 0) best = std::min(best, vu_dist(pw->player.pos, a.pos));
                return best;
            };
            pw->on_monster_hurts_player = [&](int slot, int damage, int stolen) {
                MonsterActor& a = world.actors[size_t(slot)];
                if (damage >= 0) a.hit_vol = damage;
                if (stolen > 0) a.stolen_gold += stolen;
            };
            std::ifstream f(player_file);
            std::string line;
            while (std::getline(f, line)) {
                std::istringstream ss(line);
                std::string k;
                ss >> k;
                double v[24] = {};
                for (double& x : v) if (!(ss >> x)) break;
                auto set3 = [&](float* d) { d[0] = float(v[0]); d[1] = float(v[1]); d[2] = float(v[2]); };
                if (k == "ppos") set3(pw->player.pos);
                else if (k == "pvy") pw->player.vy = float(v[0]);
                else if (k == "pyaw") pw->player.yaw = float(v[0]);
                else if (k == "pdisp") set3(pw->player.disp);
                else if (k == "cpos") set3(pw->camera.pos);
                else if (k == "cref") set3(pw->camera.ref);
                else if (k == "cnext_pos") set3(pw->camera.next_pos);
                else if (k == "cnext_ref") set3(pw->camera.next_ref);
                else if (k == "cspeed") pw->camera.speed = float(v[0]);
                else if (k == "cdist") pw->camera.dist = float(v[0]);
                else if (k == "cheight") pw->camera.height = float(v[0]);
                else if (k == "cangle") pw->camera.angle = float(v[0]);
                else if (k == "cangle_cur") pw->camera.angle_cur = float(v[0]);
                else if (k == "cfollow") set3(pw->camera.follow);
                else if (k == "cangle_h") pw->camera.angle_h = float(v[0]);
                else if (k == "pmotion") {   // time frac step frame next cur prev blending
                    MotionState& m = pw->motion;
                    m.time = float(v[0]); m.frac = float(v[1]); m.blend_step = float(v[2]); m.frame = int(v[3]); m.next = int(v[4]); m.cur = int(v[5]); m.prev = int(v[6]); m.blending = int(v[7]);
                } else if (k == "preq") {
                    pw->request = MotionRequest{float(v[0]), uint32_t(int(v[1])), int(v[2])};
                } else if (k == "pcombat") {   // mode action wish invuln busy requested
                    pw->combat.mode = int(v[0]); pw->combat.action = int(v[1]); pw->combat.wish = int(v[2]); pw->combat.invuln = int(v[3]); pw->combat.busy = int(v[4]); pw->combat.requested = int(v[5]);
                } else if (k == "pattack") {   // gauge combo_window idle_counter in_swing chain turn_to released charge charge_stage cooldown hit_window lunge lunge_dec lunge_dir(3) event_lock
                    PlayerCombat& c = pw->combat;
                    c.gauge = float(v[0]); c.combo_window = int(v[1]); c.idle_counter = int(v[2]); c.in_swing = int(v[3]); c.chain = int(v[4]); c.turn_to = float(v[5]); c.released = int(v[6]);
                    c.charge = float(v[7]); c.charge_stage = int(v[8]); c.cooldown = int(v[9]); c.hit_window = int(v[10]); c.lunge = float(v[11]); c.lunge_dec = float(v[12]);
                    c.lunge_dir[0] = float(v[13]); c.lunge_dir[1] = float(v[14]); c.lunge_dir[2] = float(v[15]); c.event_lock = int(v[16]); c.gauge_full = int(v[17]); c.can_act = int(v[18]);
                } else if (k == "pstatic") {   // a function-static counter of the game's attack code: address value
                    const unsigned a = unsigned(v[0]);
                    const int val = int(v[1]);
                    if (a == 0x2a2b7c) world.flame_cnt = val;
                    else if (a == 0x2a36a0) pw->combat.osmond_dmg_wait = val;
                    else if (a == 0x2a36a4) pw->combat.osmond_s_cnt = val;
                    else if (a == 0x2a36ac) pw->combat.osmond_msg_cnt = val;
                    else if (a == 0x2a36b4) pw->combat.osmond_wait = val;
                } else if (k == "pspeed") {   // motion index, table speed
                    const size_t idx = size_t(v[0]);
                    if (idx < pw->motion_table.size()) pw->motion_table[idx].speed = float(v[1]);
                }
            }
            pw->player.info.grounded = 1;
        }
        for (int t = 0; t < ticks; ++t) {
            if (std::getenv("DC_DBG")) std::fprintf(stderr, "tick %d\n", t);
            if (size_t(t) < feed.size()) {
                for (int k = 0; k < 3; ++k) world.player.pos[k] = float(feed[size_t(t)][size_t(k)]);
                rng.seed(uint32_t(feed[size_t(t)][3]));
                world.hold = int(feed[size_t(t)][4]);
            }
            if (pw) {   // MoveChara: the player first, with the rand state the game had when Step was entered (the player's own draws are not reproduced)
                std::copy(pw->player.pos, pw->player.pos + 3, world.player.pos);   // the monsters see the player where the last frame left him
                world.player.yaw = pw->player.yaw;
                PadRaw pad;
                for (int pt : presses)
                    if (pt <= t && t < pt + hold) pad.buttons |= kPadOK;
                for (const auto& b : buttons)
                    if (b[0] <= t && t <= b[1]) pad.buttons |= uint32_t(b[2]);
                for (const auto& st : sticks)
                    if (st[0] <= t && t <= st[1]) { pad.lx = uint8_t(st[2]); pad.ly = uint8_t(st[3]); }
                set_log.clear();
                pw->tick(pad);
                if (size_t(t) < feed.size()) rng.seed(uint32_t(feed[size_t(t)][3]));
                for (int k = 0; k < 3; ++k) world.player.prev_pos[k] = pw->prev_pos()[k];
                world.player.facing[0] = pw->combat.stick_dir[0]; world.player.facing[1] = 0.0f; world.player.facing[2] = pw->combat.stick_dir[1]; world.player.facing[3] = 1.0f;
                world.player.attack_idle = pw->combat.idle_counter;
            }
            for (const Inject& in : injections)
                if (in.tick == t) {
                    if (world.pool.set(in.radius, 0.0f, in.pos, in.damage, in.life, in.target, in.kind, in.flags, 0) >= 0) {
                        ColEntry& e = world.pool.e[size_t(world.pool.last)];
                        e.owner_kind = in.owner;
                        e.sub_id = in.sub;
                        e.status_flags = in.status;
                    }
                }
            const uint64_t draws0 = rng.draws;
            world.step_ai();
            refresh_weapon();
            if (std::getenv("DC_DBG") && world.player_fx >= 0)
                for (size_t k = 0; k < world.shots.slot[size_t(world.player_fx)].inst.size(); ++k) {
                    const auto& s = world.shots.slot[size_t(world.player_fx)].inst[k];
                    if (s.active) std::printf("pfxa %d %zu %d %.4f %d %.5f %.5f %.5f\n", t, k, s.phase, s.ms.time, s.life_counter, s.pos[0], s.pos[1], s.pos[2]);
                }
            if (pw) pw->combat.idle_counter = world.player.attack_idle;
            for (const std::string& l : world.ext_log) std::printf("xc %d %s\n", t, l.c_str());
            world.ext_log.clear();
            for (const MonsterActor& a : world.actors) {
                if (a.slot < 0 || (only >= 0 && a.slot != only)) continue;
                std::printf("%d %d %d %.9g %.9g %.9g %.5f | %.4f %.4f | %d %.7f %d | %d %d %.9g %.9g %.5f %.5f %.5f %u\n", t, a.slot, a.state, a.pos[0], a.pos[1], a.pos[2], a.rot[1], a.move_speed, a.rot_speed, a.request.id, a.motion.time, a.motion.cur, a.script_active ? 1 : 0, a.hp, a.height, a.floor_y, a.move_dir[0], a.move_dir[1], a.move_dir[2], a.vm.pc());
                std::printf("hurt %d %d %d %d %d %d\n", t, a.slot, a.hit_stop, a.poison, a.bin2, a.status_slow);
                for (int i = 0; i < 16; ++i) {
                    if (a.body_cols[size_t(i)].active) std::printf("bcol %d %d %d %.4f %.4f %.4f %.4f\n", t, a.slot, i, a.body_cols[size_t(i)].pos[0], a.body_cols[size_t(i)].pos[1], a.body_cols[size_t(i)].pos[2], a.body_cols[size_t(i)].radius);
                    if (a.dmg_cols[size_t(i)].active) std::printf("dcol %d %d %d %.4f %.4f %.4f %.4f\n", t, a.slot, i, a.dmg_cols[size_t(i)].pos[0], a.dmg_cols[size_t(i)].pos[1], a.dmg_cols[size_t(i)].pos[2], a.dmg_cols[size_t(i)].radius);
                }
            }
            for (int i = 0; i < ColPool::kSize; ++i) {
                const ColEntry& e = world.pool.e[size_t(i)];
                if (e.used) std::printf("pool %d %d %.4f %.4f %.4f %.4f %d %d %d %d %d\n", t, i, e.pos[0], e.pos[1], e.pos[2], e.radius, e.damage, e.target, e.kind, e.owner_kind, e.sub_id);
            }
            for (const CombatEvent& ev : world.events) std::printf("ev %d %d %d %d %d\n", t, int(ev.kind), ev.slot, ev.value, ev.value2);
            world.step_pool();
            if (pw) {
                pw->step_shake(rng);
                world.step_effects();
                refresh_weapon();
            }
            if (pw) pw->update_motion_request();
            world.step_motions();
            if (pw) {
                pw->step_motion();
                pw->step_status();
                world.step_shot_motions();
                world.update_lock_cursor();
                if (std::getenv("DC_DBG") && world.player_fx >= 0)
                    for (size_t k = 0; k < world.shots.slot[size_t(world.player_fx)].inst.size(); ++k) {
                        const auto& s = world.shots.slot[size_t(world.player_fx)].inst[k];
                        if (s.active) std::printf("pfx %d %zu %d %.4f %d %.5f %.5f %.5f | %.5f %.5f %.5f %d %d\n", t, k, s.phase, s.ms.time, s.life_counter, s.pos[0], s.pos[1], s.pos[2], s.vel[0], s.vel[1], s.vel[2], s.cooldown, s.wait_counter);
                    }
                for (size_t si = 0; si < world.shots.slot.size(); ++si)
                    for (size_t ii = 0; ii < world.shots.slot[si].inst.size(); ++ii) {
                        const ShotEffect::Instance& in = world.shots.slot[si].inst[ii];
                        if (in.active) std::printf("fx %d %zu %zu %d %.6f %d %d %d %.5f %.5f %.5f\n", t, si, ii, in.phase, in.ms.time, in.rq.id, int(in.rq.flags), in.life_counter, in.vel[0], in.vel[1], in.vel[2]);
                    }
                world.events.clear();
                std::printf("gold %d %d %d %d wid %d dur %g\n", t, int(world.status.get<uint16_t>(0x4346)), int(world.status.get<int16_t>(world.status.weapon_offset(0) + 0x14)), int(world.status.get<int16_t>(0x436e)), int(world.status.weapon(0).id()), double(world.status.weapon(0).durability()));
                for (int i = 0; i < Drops::kSize; ++i) {
                    const Drop& dr = world.drops.d[size_t(i)];
                    if (dr.used) std::printf("drop %d %d %.5f %.5f %.5f %d %d %d %.6f %d\n", t, i, dr.pos[0], dr.pos[1], dr.pos[2], dr.gold, dr.item, dr.state, dr.timer, dr.block);
                }
                for (const SetLog& s : set_log) {
                    if (s.target == 2) std::printf("set %d %.5f %.5f %.5f %d %.4f %d %d %d\n", t, s.pos[0], s.pos[1], s.pos[2], s.damage, s.radius, s.target, s.kind, s.life);
                    if (s.source == 1) std::printf("shotset %d %.5f %.5f %.5f %d %.4f %d %d %d\n", t, s.pos[0], s.pos[1], s.pos[2], s.damage, s.radius, s.target, s.kind, s.life);
                }
                {
                    const PlayerCombat& c = pw->combat;
                    std::printf("pvars %d %.7g %d %d %d %d %.7g %d %.7g %d %d %.7g %.7g %d %d %d\n", t, c.gauge, c.combo_window, c.idle_counter, c.in_swing, c.chain, c.turn_to, c.released, c.charge, c.cooldown,
                                c.hit_window, c.lunge, c.lunge_dec, c.can_act, c.held, c.busy);
                }
                const int ch = world.status.character();
                std::printf("player %d %.5f %.5f %.5f %.5f | %d %d %d %d %d %d | %.7f %d %d | %d %d | %.4f %.4f\n", t, pw->player.pos[0], pw->player.pos[1], pw->player.pos[2], pw->player.yaw,
                            int(world.status.life(ch)), pw->combat.mode, pw->combat.action, pw->combat.wish, pw->combat.invuln, pw->combat.busy, pw->motion.time, pw->motion.cur, pw->request.id,
                            int(world.status.get<int16_t>(0x8a68 + size_t(ch) * 2)), int(world.status.get<int16_t>(0x8a5c + size_t(ch) * 2)), pw->combat.blow[0], pw->combat.blow[2]);
            }
            std::printf("draws %d %llu\n", t, (unsigned long long)(rng.draws - draws0));
            for (const std::string& l : world.log) std::printf("  log: %s\n", l.c_str());
            world.log.clear();
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
