#include "dccore/walk.h"
#include "dccore/vu0.h"
#include "dccore/libm_ps2.h"
#include "dccore/gamemath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace dccore {

namespace {

constexpr float kPi = 3.14159274101257324f;       // DAT_002a18a0
constexpr float kTwoPi = 6.28318548202514648f;
constexpr double kPiD = 3.141592653589793;
constexpr double kTwoPiD = 6.283185307179586;

float dist3(const float* a, const float* b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void normalize3(float* v) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

}  // namespace

float pad_axis(int raw) {
    int v = raw - 0x80;
    int out;
    if (v < 0x32 && -0x32 < v) out = 0;
    else if (v < 1) out = ((raw - 0x4e) * 0x80) / 0x4e;
    else out = ((raw - 0xb1) * 0x80) / 0x4e;
    return float(out) / 128.0f;
}

float angle_interpolate(float from, float to, float param, int mode) {
    float d = to - from;
    if (kPi < d) d -= kTwoPi;
    if (d <= -kPi) d += kTwoPi;
    float step = 0.0f;
    if (mode == 0) {
        const float a = d < 0 ? -d : d;
        if (a < param) return to;
    }
    if (mode == 1) {
        step = d / param;
    } else if (mode == 0) {
        if (d < 0) {
            if (param < d) return to;
            step = 0.0f - param;
        }
        if (0.0f <= d) {
            if (d < param) return to;
            step = step + param;
        }
    }
    from += step;
    if (kPi < from) from -= kTwoPi;
    if (from <= -kPi) from += kTwoPi;
    return from;
}

float unit_rotation(float target, float current) {
    float d = target - current;
    const float ad = d <= 0.0f ? d * -1.0f : d;
    if (double(ad) <= kPiD) {
        if (double(ad) <= 0.2617993877991494) d = 0.0f;
    } else if (kTwoPiD - double(ad) <= 0.2617993877991494) {
        d = 0.0f;
    }
    constexpr float step = 0.2617993950843811f;   // 15 degrees per tick
    if (0.0f < d) {
        if (double(d) <= kPiD) current += step; else current -= step;
    }
    if (d < 0.0f) {
        if (double(d) >= -kPiD) current -= step; else current += step;
    }
    float r = target;
    if (d != 0.0f) r = current;
    if (double(r) <= -kPiD) r += kTwoPi;
    if (double(r) >= kPiD) r -= kTwoPi;
    return r;
}

int key_ctrl(float lx, float ly, float* speed) {
    int r = 0;
    if (ly != 0.0f || lx != 0.0f) {
        r = 2;
        const float a = lx < 0 ? -lx : lx, b = ly < 0 ? -ly : ly;
        const float m = a < b ? b : a;
        float sp = 0.8f * (0.1f + m);
        if (0.7f <= sp) sp = 0.7f;
        if (speed) *speed = sp;
        if (0.85f <= a + b) r = 1;
    }
    return r;
}

void camera_base_step(CameraState& c) {
    if (c.speed <= 0.0f) c.speed = 1.0f;
    for (int i = 0; i < 3; ++i) {
        if (c.speed == 1.0f) {
            c.pos[i] = c.next_pos[i];
            c.ref[i] = c.next_ref[i];
        } else {
            float step = (c.next_pos[i] - c.pos[i]) / c.speed;
            const float div = 1.0f <= c.speed ? c.speed : 1.0f;
            if (c.clamp == 1) {
                float t = 2.0f;
                if (step <= 2.0f) t = step;
                step = -2.0f;
                if (-2.0f <= t) step = t;
            }
            c.pos[i] += step;
            c.ref[i] += (c.next_ref[i] - c.ref[i]) / div;
            const float dp = c.pos[i] - c.next_pos[i], dr = c.ref[i] - c.next_ref[i];
            if (dp < c.eps && -c.eps < dp) c.pos[i] = c.next_pos[i];
            if (dr < c.eps && -c.eps < dr) c.ref[i] = c.next_ref[i];
        }
    }
    float dir[3] = {c.ref[0] - c.pos[0], c.ref[1] - c.pos[1], c.ref[2] - c.pos[2]};
    float h[3] = {dir[0], 0.0f, dir[2]};
    normalize3(h);
    c.angle_h = ps2_atan2f(-h[0], -h[2]);
    c.angle_v = -std::atan2(dir[1], std::sqrt(dir[2] * dir[2] + dir[0] * dir[0]));
}

void camera_step(CameraState& c, int ticks) {
    if (double(c.angle) > kTwoPiD) c.angle -= kTwoPi;
    if (c.angle < 0.0f) c.angle += kTwoPi;
    for (int i = 0; i < ticks; ++i) {
        if (c.follow_on) {
            float p = c.speed / 2.0f;
            float param = 1.0f;
            if (1.0f <= p) param = p;
            c.angle_cur = angle_interpolate(c.angle_cur, c.angle, param, 1);
            if (c.speed < 1.1f) c.angle_cur = c.angle;
            c.next_pos[0] = c.follow[0] + c.dist * ps2_sinf(c.angle_cur);
            c.next_pos[1] = c.follow[1] + c.height;
            c.next_pos[2] = c.follow[2] + c.dist * ps2_cosf(c.angle_cur);
            c.next_ref[0] = c.follow[0];
            c.next_ref[1] = c.follow[1];
            c.next_ref[2] = c.follow[2];
        }
        camera_base_step(c);
    }
    if (!c.follow_on) {
        c.angle = c.angle_h;
        c.angle_cur = c.angle_h;
    }
}

Walker::Walker(const Dungeon* dungeon, const FloorLayout* layout, WalkParams p) : params(p), dungeon_(dungeon), layout_(layout) {}

void Walker::place_at_start() {
    if (layout_->fixed) {
        const auto st = fixed_start(*dungeon_);
        place_at(st[0], st[2]);
        player.pos[1] = st[1] + 30.0f;
        return;
    }
    for (int y = 0; y < kGrid; ++y)
        for (int x = 0; x < kGrid; ++x)
            if (layout_->at(x, y).flags & 0x200) {
                place_at(float(x) * kCellSize, float(y) * kCellSize);
                return;
            }
}

bool Walker::place_at_stairs() {
    if (layout_->fixed) return false;
    for (int y = 0; y < kGrid; ++y)
        for (int x = 0; x < kGrid; ++x) {
            const MapCell& cell = layout_->at(x, y);
            if (!(cell.flags & 0x80)) continue;
            const int turn = ((model_turn_index(cell.rot, 0) % 4) + 4) % 4;   // the part's own turn: rotates the offset (0, 0, 2) about Y like the markers
            float dx = 0.0f, dz = 2.0f;
            switch (turn) {
                case 1: dx = 2.0f; dz = 0.0f; break;
                case 2: dz = -2.0f; break;
                case 3: dx = -2.0f; dz = 0.0f; break;
            }
            place_at(float(x) * kCellSize + dx, float(y) * kCellSize + dz);
            player.yaw = std::atan2(dx, dz);   // he walks out of the door: the camera hangs behind him
            camera.angle = camera.angle_cur = std::atan2(std::sin(player.yaw + 3.14159265f), std::cos(player.yaw + 3.14159265f));
            camera_step(camera, 1);
            for (int i = 0; i < 3; ++i) { camera.pos[i] = camera.next_pos[i]; camera.ref[i] = camera.next_ref[i]; }
            return true;
        }
    return false;
}

void Walker::place_at(float x, float z, float y) {
    player = PlayerState{};
    player.pos[0] = x;
    player.pos[1] = y;         // drop onto the floor
    player.pos[2] = z;
    camera = CameraState{};
    camera.dist = params.cam_far;
    camera.height = 5.0f;
    camera.follow_on = true;
    camera.speed = 8.0f;
    camera.angle = camera.angle_cur = 0.0f;
    camera.follow[0] = player.pos[0];
    camera.follow[1] = player.pos[1] + params.follow_height + 6.0f;
    camera.follow[2] = player.pos[2];
    camera_step(camera, 1);
    for (int i = 0; i < 3; ++i) { camera.pos[i] = camera.next_pos[i]; camera.ref[i] = camera.next_ref[i]; }
    camera_base_step(camera);
}

void Walker::auto_camera() {
    // camera collision uses the `_v` meshes of every cell within 240 units of the camera
    std::vector<CCPoly> polys = camera_source ? camera_source(camera.pos[0], camera.pos[1], camera.pos[2]) : floor_camera_polys(*dungeon_, *layout_, camera.pos[0], camera.pos[2]);
    const int n = int(polys.size());
    float* campos = camera.pos;
    float* ref = camera.ref;
    const float dirv[3] = {ref[0] - campos[0], ref[1] - campos[1], ref[2] - campos[2]};
    float end[3] = {campos[0] - ref[0], campos[1] - ref[1], campos[2] - ref[2]};
    normalize3(end);
    for (int k = 0; k < 3; ++k) end[k] = end[k] * 80.0f + ref[k];
    int idx[32];
    float hits[32][4];
    const int count = check_hits(polys.data(), n, ref, end, 32, idx, hits, 1, 0);
    far_limit_ = 80.0f;

    auto auto_move = [&](const CCPoly& poly, const float* hit) {
        (void)poly;
        float h[3] = {hit[0] - ref[0], 0.0f, hit[2] - ref[2]};
        camera.dist = 0.1f + std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
        float f2 = 0.0f;
        const float d = camera.dist - params.cam_near;
        if (d < 0.0f) f2 = -d / 10.0f;
        if (0.0f < d) f2 = d / 15.0f;
        float f3 = 2.0f;
        if (f2 <= 2.0f) f3 = f2;
        f2 = 0.05f;
        if (0.05f <= f3) f2 = f3;
        if (d < 0.0f) f2 *= 2.0f;
        camera.angle = ps2_atan2f(h[0], h[2]);
        camera.angle += 0.2f * f2;     // p1 == p2 in this call: the "else" branch
        if (camera.dist < 0.8f * params.cam_near) camera.height += 1.0f;
    };

    if (0 < count) {
        far_limit_ = dist3(ref, hits[0]);
        int last = -1;
        for (int k = 0; k < count; ++k) {
            const float v[3] = {hits[k][0] - campos[0], hits[k][1] - campos[1], hits[k][2] - campos[2]};
            if (dot3(dirv, v) < 0.0f) break;
            last = k;
        }
        if (last < 0) {
            if (dot3(dirv, polys[size_t(idx[0])].n) < 0.0f) auto_move(polys[size_t(idx[0])], hits[0]);
        } else {
            const CCPoly& poly = polys[size_t(idx[last])];
            if (0.0f < dot3(dirv, poly.n)) {
                if (last + 1 < count) {
                    const float d1 = dist3(campos, hits[last + 1]), d0 = dist3(campos, hits[last]);
                    if (d1 - d0 < 0.0f) auto_move(polys[size_t(idx[last + 1])], hits[last + 1]);
                    else auto_move(poly, hits[last]);
                } else if (far_limit_ < params.cam_near || params.cam_far <= far_limit_) {
                    auto_move(poly, hits[last]);
                } else {
                    for (int k = 0; k < 3; ++k) { camera.pos[k] = hits[last][k]; camera.next_pos[k] = hits[last][k]; }   // CCamera::SetPos
                }
            }
        }
    }
    float p[3] = {campos[0], campos[1] + 20.0f, campos[2]};
    float hit[3] = {0, 0, 0};
    const int v = check_hit_vertical(-100.0f, polys.data(), n, p, hit, 0);
    if (std::getenv("DCWALK_DEBUG")) std::fprintf(stderr, "cam %.3f %.3f %.3f v %d hit %.3f h %.4f dist %.4f far_limit %.3f count %d d_player %.3f\n", campos[0], campos[1], campos[2], v, hit[1], camera.height, camera.dist, far_limit_, count, dist3(player.pos, camera.pos));
    if (v >= 0 && campos[1] - hit[1] < 25.0f) {
        const float y = hit[1] + 25.0f;
        camera.height = (y - ref[1]) - 0.01f;
    }
    // the game measures from the CCharacter object (0x1ea1d30), which still holds the position of the previous tick at this point
    const float d = dist3(char_pos_, camera.pos);
    if (d < params.cam_up_near) {
        camera.height += 0.5f;
        if (camera.height <= 1.6f) camera.height = 1.6f;
    } else {
        if (camera.height <= 1.6f) camera.height = 1.6f;
        if (5.0f < camera.height) {
            float f14 = (camera.height - 5.0f) * 0.05f;
            float f13 = 0.15f;
            if (0.15f <= f14) f13 = f14;
            f14 = 0.5f;
            if (f13 <= 0.5f) f14 = f13;
            camera.height += -f14;
        }
    }
    if (params.cam_far < camera.dist) camera.dist += -(camera.dist - params.cam_far) / 10.0f;
    if (camera.dist < params.cam_near) camera.dist += -(camera.dist - params.cam_near) / 10.0f;
    if (camera.dist < far_limit_) camera.dist += -(camera.dist - far_limit_) / 10.0f;
}

// ---- player combat helpers (UserStatus functions the damage code uses)

namespace {
constexpr float kHeights[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};   // body height of each character (CheckHitUser span), from the overlay
}

bool Walker::status_err(uint32_t mask) const {
    if (!status) return false;
    return (status->get<uint32_t>(0x42c8 + size_t(status->character()) * 4) & mask) != 0;
}

void Walker::set_status_err(int code) {
    if (!status) return;
    const int ch = status->character();
    const size_t flags_at = 0x42c8 + size_t(ch) * 4, timer_at = 0x42e0 + size_t(ch) * 2;
    const uint32_t f = status->get<uint32_t>(flags_at);
    switch (code) {
        case 0x40:
            if ((f & 0x1c) == 0) status->set<uint32_t>(flags_at, f | 0x40);
            break;
        case 0x20:
            status->set<uint32_t>(flags_at, f | 0x20);
            break;
        case 0x10:
            if ((f & 0xc) == 0) status->set<uint32_t>(flags_at, (f | 0x10) & 0xffffffbfu);
            break;
        case 8:
            if ((f & 4) == 0) {
                status->set<uint32_t>(flags_at, f | 8);
                status->set<int16_t>(timer_at, 0x708);
            }
            break;
        case 4:
            status->set<uint32_t>(flags_at, (f | 4) & 0xffffffa7u);
            status->set<int16_t>(timer_at, 300);
            break;
        default: break;
    }
}

void Walker::add_now_life(float param, int ch, int16_t delta) {
    // CUserStatus::AddNowLife: with param != 0 the life drains toward the new value by a per-tick step (CUserStatus::Step applies it)
    const size_t life_at = 0x12 + size_t(ch) * 2, max_at = 6 + size_t(ch) * 2, target_at = 0x8a5c + size_t(ch) * 2, step_at = 0x8a68 + size_t(ch) * 2;
    if (status->get<int16_t>(step_at) != 0) {
        status->set<int16_t>(life_at, status->get<int16_t>(target_at));
        status->set<int16_t>(step_at, 0);
    }
    if (param == 0.0f) {
        int16_t life = int16_t(status->get<int16_t>(life_at) + delta);
        if (life < 1) life = 0;
        if (status->get<int16_t>(max_at) <= life) life = status->get<int16_t>(max_at);
        status->set<int16_t>(life_at, life);
        return;
    }
    int16_t target = int16_t(status->get<int16_t>(life_at) + delta);
    if (target < 1) target = 0;
    if (status->get<int16_t>(max_at) <= target) target = status->get<int16_t>(max_at);
    status->set<int16_t>(target_at, target);
    const int diff = int(target) - int(status->get<int16_t>(life_at));
    int16_t step = int16_t(gm::ftoi(gm::fmul(param, gm::fdiv(gm::itof(diff), 100.0f))));
    if (step == 0) step = diff < 0 ? int16_t(-1) : int16_t(1);
    status->set<int16_t>(step_at, step);
}

void Walker::add_drink(float param, int ch, int16_t delta) {
    // CUserStatus::AddDrink: the same for the water gauge (floats at +0x4304, maximum at +0x42ec, targets at +0x8ad6, steps at +0x8ae2)
    const size_t cur_at = 0x4304 + size_t(ch) * 4, max_at = 0x42ec + size_t(ch) * 4, target_at = 0x8ad6 + size_t(ch) * 2, step_at = 0x8ae2 + size_t(ch) * 2;
    if (status->get<int16_t>(step_at) != 0) {
        status->set<float>(cur_at, gm::itof(status->get<int16_t>(target_at)));
        status->set<int16_t>(step_at, 0);
    }
    if (param == 0.0f) {
        float v = gm::fadd(status->get<float>(cur_at), gm::itof(delta));
        if (status->get<float>(max_at) <= v) v = status->get<float>(max_at);
        status->set<float>(cur_at, v);
        return;
    }
    int16_t target = int16_t(gm::ftoi(gm::fadd(status->get<float>(cur_at), gm::itof(delta))));
    if (target < 1) target = 0;
    if (status->get<float>(max_at) <= float(target)) target = int16_t(gm::ftoi(status->get<float>(max_at)));
    status->set<int16_t>(target_at, target);
    const float diff = gm::fsub(gm::itof(target), status->get<float>(cur_at));
    int16_t step = int16_t(gm::ftoi(gm::fmul(param, gm::fdiv(diff, 100.0f))));
    if (step == 0) step = diff < 0.0f ? int16_t(-1) : int16_t(1);
    status->set<int16_t>(step_at, step);
}

void Walker::step_status() {
    if (!status || status->get<int32_t>(0x8b0c) != 0) return;
    const int ch = status->character();
    if (status->get<int32_t>(0x8b08) == 0 && !drive_step_hold) {
        // the water gauge runs dry while exploring; with nothing left the character loses a point of life per second
        float drain = gm::fmul(0.003f, gm::fadd(gm::fmul(0.2f, gm::itof(status->dungeon())), 1.0f));
        const size_t cur_at = 0x4304 + size_t(ch) * 4, bleed_at = 0x8af0 + size_t(ch) * 4;
        float cur = status->get<float>(cur_at);
        if (cur <= 0.0f) {
            cur = 0.0f;
        } else {
            if (status->get<int32_t>(0x8b10) == 0xb) drain = gm::fmul(drain, 5.0f);
            const int flags = status->weapon(ch).status_flags();
            if ((flags & 8) != 0) drain = gm::fmul(drain, 0.8f);
            if ((flags & 0x10) != 0) drain = gm::fmul(drain, 2.0f);
            cur = gm::fsub(cur, drain);
            if (cur <= 0.0f) cur = 0.0f;
        }
        status->set<float>(cur_at, cur);
        if (cur <= 0.0f) status->set<float>(bleed_at, gm::fadd(status->get<float>(bleed_at), 0.008333333767950535f));
        if (1.0f <= status->get<float>(bleed_at)) {
            status->set<float>(bleed_at, 0.0f);
            add_now_life(10.0f, ch, -1);
        }
    }
    for (int k = 0; k < 6; ++k) {   // the water gauge moves toward its target (after a drink)
        const size_t cur_at = 0x4304 + size_t(k) * 4, target_at = 0x8ad6 + size_t(k) * 2, step_at = 0x8ae2 + size_t(k) * 2;
        const int16_t step = status->get<int16_t>(step_at);
        if (step == 0) continue;
        float cur = gm::fadd(status->get<float>(cur_at), gm::itof(step));
        const float target = gm::itof(status->get<int16_t>(target_at));
        status->set<float>(cur_at, cur);
        if (step < 0 && cur <= target) { status->set<float>(cur_at, target); status->set<int16_t>(step_at, 0); }
        if (step > 0 && target <= cur) { status->set<float>(cur_at, target); status->set<int16_t>(step_at, 0); }
    }
    for (int k = 0; k < 6; ++k) {
        const size_t life_at = 0x12 + size_t(k) * 2, target_at = 0x8a5c + size_t(k) * 2, step_at = 0x8a68 + size_t(k) * 2;
        const int16_t step = status->get<int16_t>(step_at);
        if (step == 0) continue;
        const int16_t life = int16_t(status->get<int16_t>(life_at) + step);
        const int16_t target = status->get<int16_t>(target_at);
        status->set<int16_t>(life_at, life);
        if (step < 0 && life <= target) { status->set<int16_t>(life_at, target); status->set<int16_t>(step_at, 0); }
        if (step > 0 && target <= life) { status->set<int16_t>(life_at, target); status->set<int16_t>(step_at, 0); }
    }
}

int Walker::check_damage() {
    // BtCheckDamageProc: a hit box aimed at the player (target mask 1) touches the player's body span
    if (!pool || !status || combat.invuln != 0 || combat.busy != 0) return 0;
    const int ch = status->character();
    const int idx = pool->check_hit_user(kHeights[std::clamp(ch, 0, 5)], player.pos, 1);
    if (idx < 0) return 0;
    const bool was_guard = combat.mode == 6;
    combat.can_act = 1;
    combat.mode = 0;
    combat.held = 0;
    combat.lunge = 0.0f;
    combat.gauge = 100.0f;   // being hit refills the attack gauge
    combat.unit_hold = 0;
    ColEntry& e = pool->e[size_t(idx)];
    int defense = status->defense(ch);
    if (status_err(8)) defense <<= 1;
    int dmg = e.damage - defense;
    if (dmg < 1) dmg = 0;
    const int owner = e.owner_kind;
    int slot = -1;
    if (owner != -1) {
        slot = (owner - 200) / 5;
        if (slot >= 0 && slot < 16 && on_monster_hurts_player) on_monster_hurts_player(slot, dmg, 0);
    }
    const int roll = gm::ftoi(gm::fdiv(gm::fmul(gm::itof(rng ? rng->next() : 0), 100.0f), gm::kRandRange));
    if ((e.flags & 0x40000) != 0 && roll < 20 && slot != -1) {
        const uint16_t gold = status->get<uint16_t>(0x4346);
        if (10 < gold) {
            const uint16_t taken = uint16_t(gold / 5);
            status->set<uint16_t>(0x4346, uint16_t(gold - taken));
            if (on_monster_hurts_player) on_monster_hurts_player(slot, -1, taken);
        }
    }
    if ((e.flags & 0x1000) != 0 && roll < 0x41) set_status_err(8);
    if ((e.flags & 0x100000) != 0) set_status_err(4);
    if ((e.flags & 0x100) != 0 && roll < 0x41) set_status_err(4);   // (the game first looks for a protective item in the active slots)
    if ((e.flags & 0x200) != 0 && roll < 0x41 && (status->get<uint32_t>(0x42c8 + size_t(ch) * 4) & 0x10) == 0) set_status_err(0x10);
    if ((e.flags & 0x400) != 0 && roll < 0x41 && (status->get<uint32_t>(0x42c8 + size_t(ch) * 4) & 0x20) == 0) set_status_err(0x20);
    if ((e.flags & 0x800) != 0 && roll < 0x41 && (status->get<uint32_t>(0x42c8 + size_t(ch) * 4) & 0x40) == 0) set_status_err(0x40);
    if ((e.flags & 0x80000) != 0) dmg = status->max_life(ch) >> 1;
    auto face_blow = [&](const float dir[4]) {   // unitBlowActionRot: turn to face where the blow came from
        float a = ps2_atan2f(dir[0], dir[2]);
        a -= kPi;
        if (a <= -kPi) a += kTwoPi;
        player.yaw = a;
    };
    auto start_motion = [&](int id) {
        if (!motion_table.empty() && size_t(id) < motion_table.size()) motion.time = float(motion_table[size_t(id)].start);
    };
    const int kind = e.kind;
    if (kind == 3) {
        if (combat.action == 9) {   // guarding: pushed back only
            for (int k = 0; k < 3; ++k) combat.blow[k] = e.dir[k];
            combat.velo = combat.blow[0] / 10.0f;
            combat.velo_z = combat.blow[2] / 10.0f;
            if (e.target != 3) e.used = false;
        } else {
            add_now_life(10.0f, ch, int16_t(-dmg));
            for (int k = 0; k < 3; ++k) combat.blow[k] = e.dir[k];
            face_blow(e.dir);
            if (e.target != 3) e.used = false;
            if (dmg < status->life(ch)) {
                combat.mode = 5;
                combat.wish = 6;
                start_motion(6);
            } else {
                combat.mode = 4;
                combat.wish = 4;
                start_motion(4);
            }
            combat.no_interpolate = true;
            combat.lock_on = 0;
            if (lock) lock->flag = 0;
            combat.invuln = 0xa0;
        }
    }
    if (kind == 2 || kind == 4) {
        if (was_guard) {
            combat.velo = (player.pos[0] - e.pos[0]) / 2.0f;
            combat.velo_z = (player.pos[2] - e.pos[2]) / 2.0f;
            if (e.target != 3) e.used = false;
        } else {
            add_now_life(10.0f, ch, int16_t(-dmg));
            if (e.target != 3) e.used = false;
            if (0.800000011920929f <= combat.stick_vector) {
                combat.mode = 0;
                combat.wish = 0x1d;
            } else {
                combat.mode = 4;
                combat.wish = 4;
                start_motion(4);
            }
            combat.no_interpolate = true;
            combat.invuln = 0x50;
            if (kind == 4) combat.invuln = 8;
        }
    }
    return 1;
}

void Walker::update_motion_request() {
    if (motion_table.empty()) return;
    const unsigned flags = combat.no_interpolate ? 4u : 0u;
    combat.no_interpolate = false;
    if (!combat.frozen && !drive_step_hold) {
        if (combat.requested != combat.wish) {
            request = MotionRequest{-1.0f, flags, combat.wish};
            combat.requested = combat.wish;
        }
    } else {
        request = MotionRequest{-1.0f, 1u, combat.wish};
        combat.requested = -1;
    }
}

void Walker::step_motion() {
    if (motion_table.empty()) return;
    step_motion_state(motion_table, request, motion, motion_phase, &last_sample);
    update_weapon_tip();
    step_drive_counters();
}

void Walker::tick(const PadRaw& pad) {
    for (int i = 0; i < 3; ++i) char_pos_[i] = player.pos[i];
    const bool ok_on = (pad.buttons & kPadOK) != 0, ok_down = ok_on && (prev_buttons & kPadOK) == 0;
    const bool no_down = (pad.buttons & kPadNO) != 0 && (prev_buttons & kPadNO) == 0;
    const bool shift_down = (pad.buttons & kPadCamRight) != 0 && (prev_buttons & kPadCamRight) == 0;
    const bool r2_down = (pad.buttons & 2) != 0 && (prev_buttons & 2) == 0;
    prev_buttons = pad.buttons;
    const bool fighting = status && pool && !motion_table.empty();
    if (fighting && view_mode && (combat.game_task != 0 || combat.dead || r2_down || shift_down || no_down || player.pos[1] < -30.0f)) {   // leave the view
        view_mode = false;
        player.yaw = view_h;
        camera.angle = camera.angle_cur = float(double(view_h) - kPiD);
        camera.speed = 1.0f;
        camera_step(camera, 1);
    }
    if (fighting && !view_mode && r2_down && combat.invuln == 0 && combat.held == 0 && combat.game_task == 0 && !combat.dead && combat.mode == 0) {   // enter it
        view_mode = true;
        view_h = player.yaw;
        view_v = 0.0f;
        combat.wish = 0;
        combat.mode = 0;
        if (lock) lock->flag = 0;
    }
    if (fighting && combat.game_task == 200) {   // the death animation
        status->set<int32_t>(0x8b0c, 1);
        combat.wish = 0x17;
        const float end = float(motion_table[0x17].end);
        if (end - 2.0f <= motion.time && motion.time <= end) {
            combat.no_interpolate = false;
            combat.mode = 0;
            combat.game_over = true;
            combat.game_task = 0xc9;
            drive_step_hold = true;   // the game over screens stop the character's motion
        }
        if (params.camera_collision) auto_camera();
        camera_step(camera, 1);
        return;
    }
    if (fighting && combat.game_task != 0) {   // after the death animation the game over screens take over (not ported): nothing moves
        if (params.camera_collision) auto_camera();
        camera_step(camera, 1);
        return;
    }
    float lx = pad_axis(pad.lx), ly = pad_axis(pad.ly);
    const float rx = pad_axis(pad.rx), ry = pad_axis(pad.ry);
    if (view_mode) {   // EyeCamera: the stick turns the view (0.02 rad per tick), the character stands still
        view_h = float(double(view_h) - double(lx * 0.02f));
        if (view_h <= -3.14159f) view_h += 6.28319f;
        if (3.14159f < view_h) view_h -= 6.28319f;
        const float up = -ly;
        if ((up > 0.0f && view_v < 0.65f) || (up < 0.0f && view_v > -1.0f)) view_v += up * 0.02f;
        player.yaw = view_h;
        lx = 0.0f;
        ly = 0.0f;
    }
    const double theta = double(camera.angle_h);
    const float sin_t = std::sin(camera.angle_h);
    // camera-relative stick vector (ref_off of the stick: DAT_01dc4534 / DAT_01dc4538)
    float dir_x = float(double(lx) * std::cos(theta) + double(ly * sin_t));
    float dir_z = float(double(ly) * std::cos(theta) - double(lx * sin_t));
    combat.stick_dir[0] = dir_x;
    combat.stick_dir[1] = dir_z;
    camera.speed = (lock && lock->flag != 0) ? 14.0f : 8.0f;
    const bool locked_at_stick = lock && lock->flag != 0;   // (the speed of the run depends on it)
    float klx = lx, kly = ly;
    if (fighting) {
        if (status_err(0x40)) { dir_x = dir_x / 2.0f; dir_z = dir_z / 2.0f; }
        combat.frozen = 0;
        if (status_err(4)) { klx = 0.0f; dir_x = 0.0f; kly = 0.0f; dir_z = 0.0f; combat.frozen = 1; }
        combat.velo = dir_x;
        combat.velo_z = dir_z;
        {   // stickVector = DistVector of the velocity vector (x, vertical, z)
            const float sx = combat.velo, sy = player.vy, sz = combat.velo_z;
            combat.stick_vector = gm::to_float(std::sqrt(double(gm::fadd(gm::fmul(sz, sz), gm::fadd(gm::fmul(sx, sx), gm::fmul(sy, sy))))));
        }
        combat.velo = locked_at_stick ? dir_x : dir_x * params.run_speed;   // locked on: the stick is the walking speed
        combat.velo_z = locked_at_stick ? dir_z : dir_z * params.run_speed;
        combat.guard_walk = 0;
        status->set<int32_t>(0x8b08, combat.stick_vector <= 0.009999999776482582f ? 1 : 0);
    }

    float vel[3] = {locked_at_stick ? dir_x : dir_x * params.run_speed, player.vy, locked_at_stick ? dir_z : dir_z * params.run_speed};

    if ((dir_x != 0.0f || dir_z != 0.0f) && (!fighting || combat.mode == 0 || status->character() == 5)) player.yaw = unit_rotation(ps2_atan2f(dir_x, dir_z), player.yaw);
    player.motion = key_ctrl(klx, kly, &player.motion_speed);
    if (fighting) {
        const int ch = status->character();
        motion_table[0].speed = 0.05000000074505806f;
        if (player.motion != 0) motion_table[2].speed = player.motion_speed;
        combat.wish = player.motion;
        if (status->life(ch) < 1) combat.wish = 0x17;
        if (combat.invuln != 0) {
            if (0.800000011920929f <= combat.stick_vector && ch == 0) combat.wish = 0x1d;
            --combat.invuln;
        }
        if (0 < combat.busy) --combat.busy;
        if (lock) {   // NO: lock on / off; the shoulder button picks the next monster
            if (no_down) {
                if (lock->target == -1) camera.angle = float(double(player.yaw) - kPiD);
                else lock->flag = lock->flag == 0 ? 1 : 0;
            }
            if (shift_down && combat.frozen == 0 && lock->flag != 0 && shift_target) {
                if (lock->shift_no < lock->shift_rot - 1) {
                    ++lock->shift_no;
                    lock->shift_rot = shift_target(lock->shift_no);
                } else {
                    lock->shift_no = 0;
                    lock->shift_rot = shift_target(0);
                }
            }
            combat.lock_on = lock->flag;
            for (int k = 0; k < 3; ++k) lock_target[k] = lock->pos[k];
        }
        const float near_monster = nearest_monster ? nearest_monster() : 10000.0f;
        if (near_monster <= 60.0f && dir_x == 0.0f && dir_z == 0.0f) combat.wish = 0x12;   // battle stance while an enemy is close
        if (lock && lock->flag != 0 && monster_pos) {   // locked on: face the monster, strafe around it
            float tp[3];
            if (monster_pos(lock->target, tp)) {
                const float ang = ps2_atan2f(gm::fsub(tp[0], char_pos_[0]), gm::fsub(tp[2], char_pos_[2]));
                if (0.0f < combat.stick_vector) player.yaw = ang;
                else player.yaw = unit_rotation(ang, player.yaw);
                if (combat.mode == 0) {
                    combat.wish = 0x12;
                    motion_table[0x12].speed = 0.20000000298023224f;
                    float da = gm::fsub(ang, ps2_atan2f(dir_x, dir_z));
                    if (da < -3.141592025756836f) da = gm::fadd(da, 6.283184051513672f);
                    if (3.141592025756836f < da) da = gm::fsub(da, 6.283184051513672f);
                    int w = 0x12;
                    if (-1.2000000476837158f < da && da < 1.2000000476837158f) w = 0x15;
                    if (da < -2.5999999046325684f || 2.5999999046325684f < da) w = 0x16;
                    if (1.2000000476837158f < da && da < 2.5999999046325684f) w = 0x13;
                    if (da < -1.2000000476837158f && -2.5999999046325684f < da) w = 0x14;
                    if (0.0f < combat.stick_vector) {
                        float sp = 1.0f;
                        if (gm::fadd(0.20000000298023224f, combat.stick_vector) < 1.0f) sp = gm::fadd(0.20000000298023224f, combat.stick_vector);
                        combat.wish = w;
                        motion_table[size_t(w)].speed = sp;
                    }
                }
            }
        }
        if (ch == 1 && combat.action == 0xc && (dir_x != 0.0f || dir_z != 0.0f)) player.yaw = unit_rotation(ps2_atan2f(dir_x, dir_z), player.yaw);   // Xiao turns while drawing the slingshot
        if (lock && (pad.buttons & kPadCamLeft) != 0 && lock->flag != 0 && combat.frozen == 0 && combat.mode == 0) {   // the shoulder button while locked on: guard
            combat.mode = 6;
            combat.action = 8;
            combat.no_interpolate = true;
        }
        if (combat.mode == 6) {
            if (!lock || lock->flag == 0) {
                combat.mode = 0;
                combat.action = 0x12;
            }
            const float t = motion.time;
            if (combat.action == 8) {
                combat.wish = 8;
                const float end = float(motion_table[8].end);
                if (end - 1.0f <= t && t < end) {
                    combat.action = 9;
                    combat.wish = 9;
                    combat.no_interpolate = true;
                }
            }
            if (combat.action == 9) {
                combat.wish = 9;
                combat.guard_walk = 5;
                if ((pad.buttons & kPadCamLeft) == 0) {
                    combat.action = 10;
                    combat.wish = 10;
                    combat.no_interpolate = true;
                }
            }
            if (combat.action == 10) {
                combat.wish = 10;
                const float end = float(motion_table[10].end);
                if (end - 1.5f <= t && t < end) combat.mode = 0;
            }
        }
        quick_items(pad, combat.frozen != 0);
        if (pool && model && ch <= 5) {   // every character's attack
            if (ok_down && combat.frozen == 0 && combat.event_lock == 0) {
                if (ch == 0) toan_key_on();
                else if (ch == 1) xiao_key_on();
                else if (ch == 2) goro_key_on();
                else if (ch == 3) ruby_key_on();
                else if (ch == 5) osmond_key_on();
                else ungaga_key_on();
            }
            if (combat.mode == 1) {
                if (ch == 0) toan_key_play(ok_on, dir_x, dir_z);
                else if (ch == 1) xiao_key_play(ok_on);
                else if (ch == 2) goro_key_play(ok_on, dir_x, dir_z);
                else if (ch == 3) ruby_key_play(ok_on);
                else if (ch == 5) osmond_key_play(ok_on);
                else ungaga_key_play(ok_on, dir_x, dir_z);
            }
        }
        throw_step();
        drink_step();
        // ---- BtCheckDamageProc and its aftermath
        bool free_to_move = combat.mode == 0;
        if (combat.mode == 1 && ((combat.action == 0xe && ch == 0) || (combat.action == 0xd && ch == 2))) {   // charging a slash / a hammer swing: he can still walk, slowly
            if (0.10000000149011612f <= combat.stick_vector) {
                combat.velo = gm::fmul(combat.velo, 0.4000000059604645f);
                combat.velo_z = gm::fmul(combat.velo_z, 0.4000000059604645f);
                combat.wish = 0x1e;
                motion_table[0x1e].speed = gm::fadd(0.20000000298023224f, gm::fdiv(combat.stick_vector, 2.0f));
            }
            free_to_move = true;
        }
        if (combat.guard_walk == 5) {   // the guard is up: he can still walk, slowly
            if (0.10000000149011612f <= combat.stick_vector) {
                combat.velo = gm::fmul(combat.velo, 0.4000000059604645f);
                combat.velo_z = gm::fmul(combat.velo_z, 0.4000000059604645f);
                combat.wish = 0x21;
                motion_table[0x21].speed = gm::fadd(0.20000000298023224f, gm::fdiv(combat.stick_vector, 2.0f));
            }
            free_to_move = true;
        }
        if (!free_to_move || combat.can_act == 0) {   // velocities are zero unless the player is free to move
            player.vy = 0.0f;
            combat.velo = 0.0f;
            combat.velo_z = 0.0f;
        }
        check_damage();
        if (combat.mode == 4) {
            const float end = float(motion_table[4].end);
            if (motion.time < end - 2.0f || end < motion.time) combat.wish = 4;
            else combat.mode = 0;
        }
        if (combat.mode == 5) {
            const float end = float(motion_table[6].end);
            if (motion.time < end - 2.0f || end < motion.time) combat.wish = 6;
            else combat.mode = 0;
        }
        const int16_t step = status->get<int16_t>(0x8a68 + size_t(ch) * 2), target = status->get<int16_t>(0x8a5c + size_t(ch) * 2);
        const bool alive_now = (step == 0 || 0 < target) && 0 < status->life(ch);
        if (!alive_now && combat.busy == 0 && combat.mode != 5) {
            status->set_life(ch, 0);
            combat.dead = true;
            combat.frozen = 0;
            status->set<uint32_t>(0x42c8 + size_t(ch) * 4, 0);
            status->set<int16_t>(0x42e0 + size_t(ch) * 2, 0);
            combat.game_task = 200;
            camera_step(camera, 1);   // (motionDrive still steps the camera; the rest of the frame is skipped)
            return;
        }
        if (!combat.dead) {
            if (combat.mode == 5) {   // knocked down: carried by the blow, which fades
                combat.velo = combat.blow[0];
                combat.velo_z = combat.blow[2];
                for (int k = 0; k < 3; ++k) {
                    float& b = combat.blow[k];
                    if (b < 0.0f) {
                        b += combat.blow_decay;
                        if (-0.009999999776482582f <= b) b = 0.0f;
                    } else {
                        b -= combat.blow_decay;
                        if (b <= 0.009999999776482582f) b = 0.0f;
                    }
                }
            }
            if (combat.leap == 1) {   // a leap attack carries him along
                combat.velo = gm::fadd(combat.velo, combat.leap_v[0]);
                player.vy = gm::fadd(player.vy, combat.leap_v[1]);
                combat.velo_z = gm::fadd(combat.velo_z, combat.leap_v[2]);
                combat.leap_v[1] = gm::fsub(combat.leap_v[1], 0.10000000149011612f);
            }
            if (0.0f < combat.lunge) {   // the step forward of a swing
                combat.velo = gm::fadd(combat.velo, gm::fmul(combat.lunge_dir[0], combat.lunge));
                combat.velo_z = gm::fadd(combat.velo_z, gm::fmul(combat.lunge_dir[2], combat.lunge));
                combat.lunge = gm::fsub(combat.lunge, combat.lunge_dec);
                if (combat.lunge <= 0.0f) combat.lunge = 0.0f;
            }
            vel[0] = combat.velo;
            vel[2] = combat.velo_z;
        } else {
            vel[0] = 0.0f;
            vel[2] = 0.0f;
        }
        vel[1] = player.vy;
    }

    // movement against the collision polygons around the player
    std::vector<CCPoly> polys;
    if (no_floor_ticks > 0) --no_floor_ticks;
    else polys = poly_source ? poly_source(player.pos[0], player.pos[1], player.pos[2]) : floor_polys_near(*dungeon_, *layout_, player.pos[0], player.pos[2], 1);
    float out[3];
    if (fighting && monster_collide) {   // monsters are solid too
        vel[1] = player.vy;
        monster_collide(player.pos, vel, status->character());
        player.vy = vel[1];
    }
    move_check(player.pos, vel, out, player.info, polys.data(), int(polys.size()), (fighting && status->character() == 5) ? 8 : params.mask);   // (Osmond ignores a kind of polygons)
    for (int i = 0; i < 3; ++i) player.disp[i] = out[i] - player.pos[i];
    player.vy = player.vy - 0.1f;
    if (player.vy < -10.0f) player.vy = -10.0f;
    for (int i = 0; i < 3; ++i) player.pos[i] = out[i];
    if (player.info.grounded != 0) player.vy = 0.0f;
    if (player.info.foot_found != 0) combat.height_above = gm::fsub(player.pos[1], player.info.foot_hit[1]);

    // camera control by the right stick and the shoulder buttons
    camera.height += -ry;
    if (30.0f <= camera.height) camera.height = 30.0f;
    camera.angle += 0.04f * -rx;
    if (!lock || lock->flag == 0) {   // (locked on, the shoulder buttons guard and pick targets instead)
        if (pad.buttons & kPadCamLeft) camera.angle += -0.03490658476948738f;
        if (pad.buttons & kPadCamRight) camera.angle += 0.03490658476948738f;
    }
    if (pad.buttons & kPadBehind) camera.angle = float(double(player.yaw) - kPiD);
    if (player.pos[1] < -100.0f) player.pos[1] += 200.0f;
    camera.follow[0] = player.pos[0] + player.disp[0] * 7.0f;
    camera.follow[1] = player.pos[1] + params.follow_height2 + player.disp[1] * 2.0f + params.follow_height + 6.0f;
    camera.follow[2] = player.pos[2] + player.disp[2] * 7.0f;
    if (fighting && lock && lock->flag != 0 && monster_pos) {   // locked on: the camera looks a little toward the monster
        float tp[3];
        if (monster_pos(lock->target, tp)) {
            float d[3] = {gm::fsub(tp[0], player.pos[0]), gm::fsub(tp[1], player.pos[1]), gm::fsub(tp[2], player.pos[2])};
            float dist = vu0::dist(player.pos, tp);
            if (20.0f <= dist) dist = 20.0f;
            const vu0::V3 n = vu0::normalize(d);
            camera.follow[0] = gm::fadd(player.pos[0], gm::fmul(n[0], dist));
            camera.follow[1] = gm::fadd(gm::fadd(gm::fadd(params.follow_height2, player.pos[1]), 6.0f), gm::fadd(params.follow_height, 0.0f));
            camera.follow[2] = gm::fadd(player.pos[2], gm::fmul(n[2], dist));
        }
    }
    if (view_mode) {   // the camera sits at the eyes and looks along the view angles (SetPos / SetRef, no smoothing)
        static constexpr float kEye[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};
        const float h = kEye[std::clamp(status ? status->character() : 0, 0, 5)] - 1.5f;
        const float cv = std::cos(view_v);
        const float eye[3] = {player.pos[0], player.pos[1] + h, player.pos[2]};
        const float ref[3] = {eye[0] + 10.0f * std::sin(view_h) * cv, eye[1] + 10.0f * std::sin(view_v), eye[2] + 10.0f * std::cos(view_h) * cv};
        for (int k = 0; k < 3; ++k) {
            camera.pos[k] = camera.next_pos[k] = eye[k];
            camera.ref[k] = camera.next_ref[k] = ref[k];
        }
        return;
    }
    if (params.camera_collision) auto_camera();
    {   // the camera turns by itself when the stick is held sideways while running
        float input_h1 = lx;
        const float old = input_h1;
        const float disp_len = std::sqrt(player.disp[0] * player.disp[0] + player.disp[1] * player.disp[1] + player.disp[2] * player.disp[2]);
        if (camera.dist < params.cam_far) {
            input_h1 = input_h1 * ((disp_len / 1.3f) / 2.0f);
            if (0.1f < input_h1) {
                float f = 0.4f;
                if (0.4f <= old) f = old;
                float g = 1.0f;
                if (f <= 1.0f) g = f;
                camera.angle += -0.01745329238474369f * g * 2.5f;
            }
            if (input_h1 < -0.1f) {
                float f = -0.4f;
                if (old <= -0.4f) f = old;
                float g = -1.0f;
                if (-1.0f <= f) g = f;
                camera.angle += -0.01745329238474369f * g * 2.5f;
            }
        }
    }
    camera_step(camera, 1);
}

}  // namespace dccore
