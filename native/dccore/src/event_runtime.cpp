#include "dccore/event_runtime.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "dccore/event_ext_names.inc"
#include "dccore/status_ops.h"
#include "dccore/walk.h"

namespace dccore {

namespace {
constexpr float kPi = 3.14159274101257324f;
constexpr float kTwoPi = 6.28318548202514648f;

float wrap_yaw(float a) {   // the end of CActionSeq::Play: bring the yaw into (-pi, pi]
    const int n = int(a / kTwoPi);
    a = a - kPi * float(n) * 2.0f;
    if (kPi < a) a -= kTwoPi;
    if (a < -kPi) a += kTwoPi;
    return a;
}

float dist3(const float* a, const float* b) {
    const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return std::sqrt(x * x + y * y + z * z);
}
}  // namespace

const char* ev_ext_name(int id) {
    for (const EvExtName& e : kEvExtNames)
        if (e.id == id) return e.name;
    return "";
}

// ---- ActionSeq ------------------------------------------------------------------------------------------------------------------------------------------

void ActionSeq::clear() {
    move.clear();
    motion.clear();
    anime.clear();
    chara = -2;
    move_tick = 0;
    anime_tick = 0;
    rot_mode = 0;
    rot_done = false;
    rot_delay = 0;
    motion_on = false;
    motion_delay = 0;
    anime_on = false;
    anime_delay = 0;
}

bool ActionSeq::idle() const {   // CheckEnd
    const bool motion_ok = motion.empty() || motion.front().kind != 7;
    return move.empty() && motion_ok && anime.empty();
}

// CActionSeq::Play
void EventRuntime::asq_play(ActionSeq& q) {
    if (q.move.empty() && q.motion.empty() && q.anime.empty()) return;
    while (!q.move.empty()) {
        AsqNode& n = q.move.front();
        if (n.type == kAsqMove) {
            ++q.move_tick;
            if (q.move_tick < n.value) {
                const float t = float(q.move_tick) / float(n.value);
                for (int k = 0; k < 3; ++k) q.pos[k] = q.prev[k] + (n.v[k] - q.prev[k]) * t;
            } else {
                for (int k = 0; k < 3; ++k) { q.pos[k] = n.v[k]; q.prev[k] = n.v[k]; }
                q.move.pop_front();
                q.move_tick = 0;
            }
            break;
        }
        if (n.type == kAsqWaitRot) {
            if (q.rot_done) {
                q.rot_mode = 0;
                q.rot_done = false;
                q.move.pop_front();
            }
            break;
        }
        if (n.type == kAsqClearRot) {
            q.rot_mode = 0;
            q.rot_done = false;
            q.move.pop_front();
        } else if (n.type == kAsqRotMove) {
            q.rot_mode = 2;
            q.rot_done = false;
            for (int k = 0; k < 4; ++k) q.rot_target[k] = n.v[k];
            q.move.pop_front();
        } else if (n.type == kAsqDelayRot) {
            q.rot_delay = n.value;
            q.move.pop_front();
        } else if (n.type == kAsqRotAngle) {
            q.rot_mode = 3;
            q.rot_done = false;
            for (int k = 0; k < 4; ++k) q.rot_target[k] = n.v[k];
            q.move.pop_front();
        } else if (n.type == kAsqRotRef) {
            q.rot_mode = 1;
            q.rot_done = false;
            for (int k = 0; k < 4; ++k) q.rot_target[k] = n.v[k];
            q.move.pop_front();
        } else if (n.type == kAsqSetRot) {
            for (int k = 0; k < 4; ++k) { q.rot[k] = n.v[k]; q.rot_prev[k] = n.v[k]; }
            q.move.pop_front();
            q.move_tick = 0;
        } else if (n.type == kAsqSetPos) {
            for (int k = 0; k < 4; ++k) { q.pos[k] = n.v[k]; q.prev[k] = n.v[k]; }
            q.move.pop_front();
            q.move_tick = 0;
        } else if (n.type == kAsqAnimeTrg) {
            q.anime_on = true;
            q.anime_delay = n.value;
            q.move.pop_front();
        } else if (n.type == kAsqMotionTrg) {
            if (q.motion_on) {
                if (!q.motion.empty()) q.motion.pop_front();
            }
            if (!q.motion.empty()) {
                q.motion_on = true;
                q.motion_delay = n.value;
            }
            q.move.pop_front();
        } else {
            break;
        }
    }
    // (the rotation queue of the original is never filled)
    if (q.rot_mode != 0 && q.rot_delay == 0) {
        float fx = 0, fz = 1;
        if (q.rot_mode == 1) { fx = q.rot_target[0] - q.pos[0]; fz = q.rot_target[2] - q.pos[2]; }
        if (q.rot_mode == 4) { fx = q.rot_target[0]; fz = q.rot_target[2]; }
        if (q.rot_mode == 2) { fx = q.pos[0] - q.prev[0]; fz = q.pos[2] - q.prev[2]; }
        if (q.rot_mode == 3) {
            q.rot[1] = angle_interpolate(q.rot[1], q.rot_target[1], q.rot_target[3], 0);
            if (std::fabs(q.rot[1] - q.rot_target[1]) <= 0.001f || std::fabs(std::fabs(q.rot[1] - q.rot_target[1]) - kTwoPi) <= 0.001f) q.rot_done = true;
        } else if (fx == 0.0f && fz == 0.0f) {
            q.rot_done = true;
        } else {
            const float target = std::atan2(fx, fz);
            q.rot[1] = angle_interpolate(q.rot[1], target, q.rot_target[3], 0);
            const float d = std::fabs(q.rot[1] - target);
            if (d <= 0.001f || std::fabs(d - kTwoPi) <= 0.001f) q.rot_done = true;
        }
    }
    if (--q.rot_delay < 0) q.rot_delay = 0;

    EvActor* act = q.chara >= -1 && q.chara < 16 ? &actor(q.chara) : nullptr;
    if (act) {
        // a "next" motion node leaves the queue when the character's motion with that number is near its end
        if (!q.motion.empty() && q.motion.front().a == act->motion && q.motion.front().kind == 7) {
            float start = 0, end = 10, blend = 0;
            const bool known = host_->actor_motion_info(q.chara, act->motion, start, end, blend);
            const float now = host_->actor_motion_time(q.chara);
            if (known && now < end && start < now && (end - blend) - 0.01f <= now) q.motion.pop_front();
        }
        if (q.motion_on && q.motion_delay < 1 && !q.motion.empty()) {
            const AsqNode& m = q.motion.front();
            int flags = (m.kind == 7 || m.kind == 6) ? 2 : 0;
            if (act->motion != m.a) flags |= m.mflags;
            if (act->motion != m.a || act->motion_flags != flags || act->motion_speed != m.speed) ++act->motion_serial;
            act->motion = m.a;
            act->motion_flags = flags;
            act->motion_speed = m.speed;
        }
        if (q.anime_on && q.anime_delay < 1) {
            bool done = false;
            while (!done && !q.anime.empty()) {
                const AsqNode& n = q.anime.front();
                if (n.type != kAsqAnime) { q.anime.pop_front(); continue; }
                auto apply = [&]() {
                    if (n.a == -1 && n.b == 0) act->anime_mask = 0;
                    else if (n.a >= 0 && n.a < 32) {
                        if (n.b) act->anime_mask |= 1u << n.a; else act->anime_mask &= ~(1u << n.a);
                    }
                };
                if (n.value == 0) {
                    apply();
                    q.anime.pop_front();
                    q.anime_tick = 0;
                    continue;
                }
                if (n.value <= q.anime_tick) {
                    if (n.speed != 0 && n.a >= 0 && n.a < 32) act->anime_mask &= ~(1u << n.a);
                    q.anime.pop_front();
                    q.anime_tick = 0;
                    continue;
                }
                apply();
                done = true;
            }
            ++q.anime_tick;
            if (q.anime.empty()) q.anime_on = false;
        }
    }
    if (--q.motion_delay < 0) q.motion_delay = 0;
    if (--q.anime_delay < 0) q.anime_delay = 0;
    if (act) {
        act->pos = {q.pos[0], q.pos[1], q.pos[2]};
        act->rot = {q.rot[0], wrap_yaw(q.rot[1]), q.rot[2]};
        q.rot[1] = q.rot[1];
    }
}

// ---- MesWindow ------------------------------------------------------------------------------------------------------------------------------------------

void MesWindow::make(int id, int num, const std::vector<int>& names, const std::vector<int>& values) {
    message_id = id;
    number = num;
    pages.clear();
    page = 0;
    reveal = 0;
    waiting_key = false;
    want_open = true;
    if (!bank) return;
    Page cur;
    auto push_text = [&](const std::string& s) {
        cur.text += s;
        for (unsigned char c : s) if ((c & 0xC0) != 0x80 && c != ' ' && c != '\n') ++cur.glyphs;
    };
    for (const MesToken& t : mes_tokens(*bank, id)) {
        switch (t.kind) {
            case MesToken::Glyph: push_text(t.text); break;
            case MesToken::Space: cur.text += ' '; break;
            case MesToken::LineFeed: cur.text += '\n'; break;
            case MesToken::PageBreak: pages.push_back(std::move(cur)); cur = Page(); break;
            case MesToken::HeroName: push_text(size_t(t.arg) < hero_names.size() ? hero_names[size_t(t.arg)] : "?"); break;
            case MesToken::SysSlot:
                if (system_bank && size_t(t.arg) < names.size() && names[size_t(t.arg)] >= 0) push_text(system_bank->get(names[size_t(t.arg)], hero_names));
                break;
            case MesToken::Number: push_text(std::to_string(num)); break;
            case MesToken::ValueSlot: push_text(size_t(t.arg) < values.size() ? std::to_string(values[size_t(t.arg)]) : "0"); break;
            default: break;
        }
    }
    pages.push_back(std::move(cur));
}

int MesWindow::state() const {
    if (openness <= 0.0f && !want_open) return Closed;
    if (openness <= 0.0f) return Opening;
    if (openness < 1.0f) return want_open ? Opening : Closing;
    if (!want_open) return Closing;
    if (waiting_key) return PageWait;
    if (page < pages.size() && reveal < float(pages[page].glyphs)) return Printing;
    return Complete;
}

void MesWindow::step() {
    const float rate = 0.125f;
    if (want_open) openness = std::min(1.0f, openness + rate);
    else openness = std::max(0.0f, openness - rate);
    if (openness < 1.0f || !want_open || page >= pages.size() || waiting_key) return;
    const float total = float(pages[page].glyphs);
    if (speed <= 0.0f) reveal = total;
    else reveal = std::min(total, reveal + speed);
    if (reveal >= total && !last_page()) waiting_key = true;
}

void MesWindow::next_page() {
    if (!waiting_key) return;
    ++page;
    reveal = 0;
    waiting_key = false;
}

void MesWindow::close() {
    speed = default_speed;
    message_id = -1;
    want_open = false;
}

std::string MesWindow::visible_text() const {
    if (page >= pages.size()) return {};
    const std::string& s = pages[page].text;
    int shown = int(reveal);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\n') { out += char(c); continue; }
        if ((c & 0xC0) != 0x80) {
            if (shown <= 0) break;
            --shown;
        }
        out += char(c);
    }
    return out;
}

// ---- EventRuntime ---------------------------------------------------------------------------------------------------------------------------------------

EventRuntime::EventRuntime(std::shared_ptr<ScriptProgram> program, EventHost* host) : host_(host), program_(std::move(program)) {
    vm_.load(program_);
    vm_.set_external([this](int32_t id, const ScriptArgs& a) { return ext(id, a); });
    reset_event_state();
}

EvVec3 EventRuntime::to_world(EvVec3 p) const {
    const float c = std::cos(world_rot.y), s = std::sin(world_rot.y);
    return {p.x * c + p.z * s + world_pos.x, p.y + world_pos.y, -p.x * s + p.z * c + world_pos.z};
}

EvVec3 EventRuntime::from_world(EvVec3 p) const {
    const float x = p.x - world_pos.x, z = p.z - world_pos.z;
    const float c = std::cos(world_rot.y), s = std::sin(world_rot.y);
    return {x * c - z * s, p.y - world_pos.y, x * s + z * c};
}

float EventRuntime::convert_time(float hours) {
    float t = hours - 10.0f;
    if (t < 0.0f) t += 24.0f;
    return t / 2.0f;
}

float EventRuntime::invert_time(float t) {
    float h = t * 2.0f + 10.0f;
    if (h >= 24.0f) h -= 24.0f;
    return h;
}

int EventRuntime::time_bucket() const {
    const float t = save ? save->now_time() : 0.0f;
    if (t >= 11.5f || t < 2.5f) return 0;
    if (t < 5.5f) return 1;
    if (t < 8.5f) return 2;
    return 3;
}

void EventRuntime::reset_event_state() {   // EdInitEventParam(Simple)
    for (EvActor& a : actors) {
        a.draw = true;
        a.shadow = true;
        a.collide = false;
        a.stop = false;
        a.draw_before = false;
        a.foot_sound = 1;
    }
    for (ActionSeq& q : asq) q.clear();
    for (EvHandle& h : handles) h = EvHandle();
    world_pos = world_rot = EvVec3();
    camera.sync_ref_chara = -2;
    camera.sync_ref_obj = camera.sync_pos_obj = -1;
    camera.follow_chara = -2;
    camera.projection = 0;
    exclamation = false;
    for (MesWindow& w : windows) {
        w.speed = w.default_speed;
        w.pos_mode = 0;
        w.tail = 0;
        w.fukidashi = 0;
        w.cursor = -1;
        w.okuri = true;
        w.autoset_a = w.autoset_b = -2;
    }
    for (int& f : work_flags) f = 0;
    draw_sprites.clear();
    for (EvSprite& sp : sprites) sp = EvSprite();
    filter_on = false;
    back_on = false;
    ambient_on = false;
    for (EvLight& l : lights) l.on = false;
    thunder = false;
    sprite_textures.clear();
}

bool EventRuntime::start(int function_id) {
    result_code_ = 0;
    nav_ = EvNav();
    next_event_ = -1;
    menu_mode_ = 0;
    reset_event_state();
    for (size_t i = 0; i < windows.size(); ++i) {
        windows[i].bank = i == 0 ? (has_talk_bank ? &talk_bank : nullptr) : (has_event_bank ? &event_bank : nullptr);
        windows[i].system_bank = &system_bank;
        windows[i].hero_names = hero_names;
        windows[i].speed = windows[i].default_speed = msg_speed;
    }
    const int r = vm_.run(function_id);
    return r == 1;
}

void EventRuntime::skip() { vm_.skip(); }

void EventRuntime::item_message(int item) {
    system_message.active = true;
    system_message.text = "You got " + (items ? system_bank.get(items->name_message(item), hero_names) : item_text_id_name(system_bank, item)) + "!";
    system_message_ticks = 150;
}

void EventRuntime::modal_done(int value) {
    if (modal_result_slot_ >= 0 && size_t(modal_result_slot_) < vm_.stack().size()) vm_.stack()[size_t(modal_result_slot_)].value = value;
    menu_mode_ = 0;
    modal_result_slot_ = -1;
}

void EventRuntime::begin_modal(int kind, const std::vector<int>& args, const ScriptArgs& a) {
    menu_mode_ = kind;
    modal_args_ = args;
    modal_result_slot_ = a.n > 0 && a.type(0) == 3 ? a.vm->stack()[size_t(a.base)].value : -1;
    host_->open_modal(kind, args);   // (a front end may answer at once)
}

// ObjAnimePlay: the current value goes to the handle, then it moves on by the speed.
void EventRuntime::step_obj_anime() {
    auto beyond = [](float cur, float end, float speed) { return 0.0f < speed ? end < cur : cur < end; };   // end_check
    for (ObjAnime& q : obj_anime) {
        if (!q.active) continue;
        if (q.handle >= 0 && q.handle < 32 && handles[size_t(q.handle)].kind != 0) {
            EvHandle& hd = handles[size_t(q.handle)];
            if (q.kind == 0) { hd.rot = {q.cur[0], q.cur[1], q.cur[2]}; hd.rot_set = true; }
            else if (q.kind == 1) { hd.pos = {q.cur[0], q.cur[1], q.cur[2]}; hd.pos_set = true; }
            else if (q.kind == 2) hd.scale = {q.cur[0], q.cur[1], q.cur[2]};
            hd.moved = true;
        }
        for (int k = 0; k < 3; ++k) q.cur[k] += q.speed[k];
        if (q.mode == 0) continue;
        for (int k = 0; k < 3; ++k) {
            if (!beyond(q.cur[k], q.end[k], q.speed[k])) continue;
            if (q.mode == 1) q.cur[k] = q.start[k];
            else if (q.mode == 2) { q.speed[k] = -q.speed[k]; std::swap(q.start[k], q.end[k]); q.cur[k] = q.start[k] + q.speed[k]; }
            else if (q.mode == 3) { q.cur[k] = q.end[k]; q.speed[k] = 0.0f; }
        }
    }
}

bool EventRuntime::step() {
    ++frames;
    for (MesWindow& w : windows) w.step();
    bool alive = true;
    if (menu_mode_ == 0) {
        if (vm_.running()) alive = vm_.resume() == 1;
        else alive = false;
    }
    for (ActionSeq& q : asq) asq_play(q);
    step_obj_anime();
    step_fade();
    step_camera();
    if (result_code_ > 0) alive = false;
    return alive || menu_mode_ != 0;
}

void EventRuntime::step_fade() {
    for (EvSprite& s : sprites) { s.x += s.vx; s.y += s.vy; }
    if (system_message_ticks > 0 && --system_message_ticks == 0 && !in_event_message) system_message.active = false;
    if (!fade.active) return;
    if (fade.out) {
        fade.alpha = std::min(128.0f, fade.alpha + fade.step);
        if (fade.alpha >= 128.0f) { fade.end = true; fade.active = false; }
    } else {
        fade.alpha = std::max(0.0f, fade.alpha - fade.step);
        if (fade.alpha <= 0.0f) { fade.end = true; fade.active = false; }
    }
}

EvVec3 EventRuntime::actor_pos_world(int slot) const {
    if (slot < -1 || slot > 15) return {};
    return actor(slot).pos;
}

void EventRuntime::step_camera() {
    EvCamera& c = camera;
    CameraState& s = c.cs;
    if (c.follow_chara >= -1) {
        const EvActor& a = actor(c.follow_chara);
        s.follow[0] = a.pos.x;
        s.follow[1] = a.pos.y + a.body[1] * 0.8f;
        s.follow[2] = a.pos.z;
    }
    if (c.sync_ref_chara >= -1) {
        const EvActor& a = actor(c.sync_ref_chara);
        s.next_ref[0] = a.pos.x + c.sync_ref_offset.x;
        s.next_ref[1] = a.pos.y + a.body[1] * 0.8f + c.sync_ref_offset.y;
        s.next_ref[2] = a.pos.z + c.sync_ref_offset.z;
    }
    camera_step(s, 1);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// the external functions

namespace {
uint32_t swap_ok_cancel(uint32_t m) {
    const uint32_t ok = m & 0x40, cancel = m & 0x20;
    return (m & ~0x60u) | (ok ? 0x20u : 0u) | (cancel ? 0x40u : 0u);
}
}  // namespace

bool EventRuntime::ext(int id, const ScriptArgs& a) {
    const int n = a.n;
    if (trace) trace(id, ev_ext_name(id), a);
    if (extra_ext && extra_ext(id, a)) return true;
    auto I = [&](int i) { return i < n ? a.get_int(i) : 0; };
    auto F = [&](int i) { return i < n ? a.get_float(i) : 0.0f; };
    auto V3 = [&](int i) { return EvVec3{F(i), F(i + 1), F(i + 2)}; };
    auto W3 = [&](int i) { return to_world(V3(i)); };   // GetPosition: script coordinates -> world
    auto put3 = [&](int i, EvVec3 v) { a.set_float(i, v.x); a.set_float(i + 1, v.y); a.set_float(i + 2, v.z); };
    auto slot_ok = [&](int s) { return s >= -1 && s < 16; };
    auto win = [&](int w) -> MesWindow* { return w >= 0 && w < 8 ? &windows[size_t(w)] : nullptr; };
    auto chan = [&](int c) -> ActionSeq* { return c >= 0 && c < 10 ? &asq[size_t(c)] : nullptr; };

    switch (id) {
        // ---- pad
        case 0: if (n >= 1) a.set_int(0, int32_t(swap_ok_cancel(pad_on))); return n >= 1;
        case 1: if (n >= 1) a.set_int(0, int32_t(swap_ok_cancel(pad_down))); return n >= 1;
        case 2: if (n >= 1) a.set_int(0, int32_t(swap_ok_cancel(pad_up))); return n >= 1;
        case 903:   // _GET_APAD
            for (int k = 0; k < std::min(n, 4); ++k) a.set_float(k, stick[k]);
            return true;
        case 4: if (n >= 1) a.set_int(0, rng ? rng->next() : std::rand()); return true;   // _GET_RANDOM
        // ---- flow
        case 3: result_code_ = I(0); return true;                                   // _SET_RETURN_CODE
        case 5: next_event_ = I(0); return true;                                    // _NEXT_EVENT
        case 6:                                                                     // _GOTO_INTERIOR(parts, "name" [, event])
            nav_.kind = EvNav::Interior;
            nav_.parts = I(0);
            nav_.name = n > 1 ? a.get_string(1) : "";
            nav_.event = n > 2 ? I(2) : -1;
            result_code_ = 4;
            return true;
        case 7:                                                                     // _SET_WORLD_COORD
            if (n == 6) { world_pos = V3(0); world_rot = V3(3); }
            else { world_pos = EvVec3(); world_rot = EvVec3(); }
            return true;
        case 8: { const EvVec3 wp = world_pos, wr = world_rot; reset_event_state(); world_pos = wp; world_rot = wr; return true; }   // _INITIALIZE (the world coordinate system set by the script stays: the Xiao jump scene calls it after _SET_WORLD_COORD)
        case 9: return true;
        case 10: exclamation = true; return true;                                   // _DRAW_EXCLAMATION_MARK
        case 11: case 25: {                                                         // _GOTO_USE_ITEM / _GOTO_CHANGE_ESA
            std::vector<int> args;
            for (int i = 1; i < n; ++i) args.push_back(I(i));
            menu_mode_ = id == 11 ? 5 : 9;
            modal_args_ = args;
            modal_result_slot_ = n > 0 && a.type(0) == 3 ? a.vm->stack()[size_t(a.base)].value : -1;
            host_->open_modal(menu_mode_, args);
            return true;
        }
        case 12: skip_enable_ = I(0) != 0; return true;                             // _SKIP
        case 13:                                                                    // _GOTO_OUTSIDE(event)
            nav_.kind = EvNav::Outside;
            nav_.event = I(0);
            result_code_ = 7;
            return true;
        case 14:                                                                    // _FINISH: end-of-event cleanup
            for (ActionSeq& q : asq) q.clear();
            return true;
        case 15:                                                                    // _MAP_JUMP(map [, event])
            nav_.kind = EvNav::MapJump;
            nav_.map = I(0);
            nav_.event = n > 1 ? I(1) : -1;
            result_code_ = 8;
            return true;
        case 16: if (n >= 1) a.set_int(0, old_map_no + 1); return true;             // _GET_OLD_MAPNO
        case 17: next_event_ = I(0); return true;                                   // _FADEOUT_TO_EVENT
        case 18: case 20: case 24: case 26: {                                       // modal screens
            menu_mode_ = id == 18 ? 6 : id == 20 ? 7 : id == 24 ? 8 : 10;
            modal_args_.clear();
            if (id == 18) modal_args_.push_back(I(0));
            modal_result_slot_ = id == 20 && n > 0 && a.type(0) == 3 ? a.vm->stack()[size_t(a.base)].value : -1;
            host_->open_modal(menu_mode_, modal_args_);
            return true;
        }
        case 21: nav_.floor = I(0); return true;                                    // _SET_DUNGEON_FLOOR
        case 23: case 27: return true;                                              // _MAP_JUMP_BGM_STOP / PLAY
        // ---- files and characters
        case 32: current_dir_ = n > 0 ? a.get_string(0) : ""; return true;          // _SET_CURRENT_DIR
        case 33:                                                                    // _LOAD_CHR_FILE
            packs_.clear();
            for (int i = 0; i < n && i < 16; ++i) {
                std::string name = a.get_string(i);
                const std::string path = !name.empty() && name[0] == '/' ? name.substr(1) : current_dir_ + name;
                packs_.push_back(path);
                host_->load_pack(i, path);
            }
            return true;
        case 34: if (n >= 1) a.set_int(0, 0); return true;                          // _LOAD_SYNC (loads are synchronous)
        case 44: active_file_ = I(0); active_buffer_ = I(1); return true;           // _ACTIVE_FILE_BUFFER
        case 35: {                                                                  // _LOAD_CHARA(chara, "entry")
            const int s = I(0);
            if (!slot_ok(s)) return false;
            EvActor& ac = actor(s);
            ac.entry = n > 1 ? a.get_string(1) : "";
            ac.pack = size_t(active_file_) < packs_.size() ? packs_[size_t(active_file_)] : "";
            ac.loaded = host_->load_actor(s, active_file_, ac.entry);
            ++ac.generation;
            ac.draw = ac.shadow = true;
            ac.ambient[3] = 128.0f;
            ac.anime_mask = 0;
            ac.motion = 0;
            ac.motion_speed = -1.0f;
            ac.motion_flags = 0;
            ++ac.motion_serial;
            return ac.loaded;
        }
        case 36: {                                                                  // _LOAD_CHARA_TEXTURE(chara, entries...)
            std::vector<std::string> e;
            for (int i = 1; i < n; ++i) e.push_back(a.get_string(i));
            host_->load_textures(I(0), active_file_, e);
            return true;
        }
        case 37: case 71: {                                                         // _LOAD_IN_VILLAGER(slot, info, "file" [, x,y,z, rot])
            const int s = I(0);
            if (!slot_ok(s) || s < 0) return false;
            EvActor& ac = actor(s);
            ac.villager_info = I(1);
            ac.info_id = I(1);
            ac.entry = n > 2 ? a.get_string(2) : "";
            ac.pack = current_dir_;
            ac.loaded = true;
            ++ac.generation;
            host_->load_villager(s, I(1), ac.entry);
            if (n >= 6) { ac.pos = W3(3); }
            if (n >= 7) ac.rot.y = world_yaw(F(6));
            return true;
        }
        case 38: case 39: case 40: return true;                                     // buffers
        case 41: {                                                                  // _LOAD_SCENE(scene, "entry" [, speed])
            const int k = I(0);
            if (k < 0 || k >= 4) return false;
            scenes[size_t(k)].entry = n > 1 ? a.get_string(1) : "";
            scenes[size_t(k)].speed = n > 2 ? F(2) : 1.0f;
            scenes[size_t(k)].pack = size_t(active_file_) < packs_.size() ? packs_[size_t(active_file_)] : "";
            scenes[size_t(k)].loaded = host_->load_scene(k, active_file_, scenes[size_t(k)].entry, scenes[size_t(k)].speed);
            return true;
        }
        case 42: {                                                                  // _SYNC_SCENE_CHARA(scene, "frame", chara)
            const int s = I(2);
            if (!slot_ok(s)) return false;
            EvActor& ac = actor(s);
            ac.attach_kind = 1;
            ac.attach_target = I(0);
            ac.attach_frame = n > 1 ? a.get_string(1) : "";
            return true;
        }
        case 43: if (I(0) >= 0 && I(0) < 4) scenes[size_t(I(0))].camera_sync = true; return true;      // _SYNC_SCENE_CAMERA
        case 46: if (I(0) >= 0 && I(0) < 4) scenes[size_t(I(0))].camera_sync = false; return true;     // _RELEASE_SCENE_CAMERA
        case 45: if (slot_ok(I(0))) actor(I(0)).attach_kind = 0; return true;                          // _RELEASE_SCENE_CHARA
        case 47: case 48:                                                           // _DELETE_CHARA / _INIT_CHARA
            for (int i = 0; i < n; ++i) {
                const int s = I(i);
                if (!slot_ok(s)) continue;
                actor(s).loaded = false;
                actor(s).draw = actor(s).shadow = false;
                host_->unload_actor(s);
            }
            return true;
        case 49: case 50: case 53: case 52: return true;                            // item models: not needed yet
        case 51: {                                                                  // _SYNC_CHARA_ITEM(chara, "frame", item)
            if (!slot_ok(I(0))) return true;
            EvActor& ac = actor(I(0));
            (void)ac;
            return true;
        }
        case 54: {                                                                  // _SYNC_CHARA_CHARA(a, "frame", b)
            const int b = I(2);
            if (!slot_ok(b)) return false;
            EvActor& ac = actor(b);
            ac.attach_kind = 2;
            ac.attach_target = I(0);
            ac.attach_frame = n > 1 ? a.get_string(1) : "";
            return true;
        }
        case 55: if (slot_ok(I(0))) actor(I(0)).attach_kind = 0; return true;       // _RELEASE_CHARA_CHARA
        case 73: if (I(0) >= 0 && I(0) < 4) { scenes[size_t(I(0))].pos = W3(1); return true; } return false;                        // _SET_SCENE_POS
        case 75: if (I(0) >= 0 && I(0) < 4) { const EvVec3 r = V3(1); scenes[size_t(I(0))].rot = {r.x, world_yaw(r.y), r.z}; return true; } return false;   // _SET_SCENE_ROT
        case 56: case 72: case 74: {                                                // _LOAD_SPRITE_TEXTURE / _LOAD_BG_SPRITE_TEXTURE / _LOAD_TEXTURE(blk, entries...)
            std::vector<std::string> e;
            for (int i = id == 74 ? 1 : 0; i < n; ++i) e.push_back(a.get_string(i));
            for (const std::string& s : e) sprite_textures.push_back(s);
            host_->load_textures(id == 74 ? I(0) : 0, active_file_, e);
            return true;
        }
        case 57: host_->villagers_load(); return true;                              // _LOAD_VILLAGER
        case 58: if (n >= 2) a.set_int(1, host_->villager_present(I(0))); return true;       // _CHECK_IN_VILLAGER
        case 59: host_->villager_appear(0, I(0)); return true;
        case 60: host_->villager_appear(1, I(0)); return true;
        case 62: host_->villager_appear(2, I(0)); return true;
        case 61: host_->villager_appear(3, I(0)); return true;
        case 70: return true;
        case 66: {                                                                  // _TURN_CHARA(x,y,z, speed): the hero faces a point
            EvActor& h = actor(-1);
            const EvVec3 p = W3(0);
            const float dx = p.x - h.pos.x, dz = p.z - h.pos.z;
            if (dx != 0.0f || dz != 0.0f) h.rot.y = angle_interpolate(h.rot.y, std::atan2(dx, dz), F(3) <= 0 ? 0.1f : F(3), 0);
            return true;
        }
        case 136: {                                                                 // _TURN_NPC(chara, x,y,z, speed)
            if (!slot_ok(I(0))) return false;
            EvActor& h = actor(I(0));
            const EvVec3 p = W3(1);
            const float dx = p.x - h.pos.x, dz = p.z - h.pos.z;
            if (dx != 0.0f || dz != 0.0f) h.rot.y = angle_interpolate(h.rot.y, std::atan2(dx, dz), F(4) <= 0 ? 0.1f : F(4), 0);
            return true;
        }
        case 67: actor(-1).pos = W3(0); return true;                                // _SET_CHARA_POS
        case 68:                                                                    // _SET_CHARA_ROT
            if (n >= 3) { const EvVec3 r = V3(0); actor(-1).rot = {r.x, world_yaw(r.y), r.z}; }
            else actor(-1).rot.y = world_yaw(F(0));
            return true;
        case 69: { const EvVec3 r = actor(-1).rot; a.set_float(0, r.x); a.set_float(1, r.y - world_rot.y); a.set_float(2, r.z); return n >= 3; }
        case 64: put3(0, from_world(actor(-1).pos)); return n >= 3;
        case 131: if (!slot_ok(I(0)) || n < 4) return false; put3(1, from_world(actor(I(0)).pos)); return true;   // _GET_NPC_POS
        case 132: if (n >= 2) a.set_int(1, host_->villager_part(I(0))); return true;
        case 133: {                                                                 // _SET_NPC_MOTION(chara, motion [, speed] [, mode])
            if (!slot_ok(I(0))) return false;
            EvActor& ac = actor(I(0));
            ac.motion = I(1);
            ac.motion_flags = n == 4 ? I(3) : 0;
            ac.motion_speed = n >= 3 ? F(2) : -1.0f;
            ++ac.motion_serial;
            return true;
        }
        case 134: {                                                                 // _SGET_NPC_TALK_MES(grd_npc, &out)
            if (n < 2 || !save) return false;
            const uint8_t* d = save->grd_data(map_no);
            const int k = I(0);
            a.set_int(1, d && k >= 0 && k < 20 ? *reinterpret_cast<const int32_t*>(d + 0xac8 + size_t(k) * 0xc + 8) : 0);
            return true;
        }
        case 135: {                                                                 // _SSET_NPC_TALK_MES(grd_npc, v)
            if (!save) return false;
            uint8_t* d = save->grd_data(map_no);
            const int k = I(0);
            if (d && k >= 0 && k < 20) { const int32_t v = I(1); std::memcpy(d + 0xac8 + size_t(k) * 0xc + 8, &v, 4); }
            return true;
        }
        case 137: if (!slot_ok(I(0))) return false; actor(I(0)).pos = W3(1); return true;          // _SET_NPC_POS
        case 138:                                                                   // _SET_NPC_ROT
            if (!slot_ok(I(0))) return false;
            if (n >= 4) { const EvVec3 r = V3(1); actor(I(0)).rot = {r.x, world_yaw(r.y), r.z}; }
            else actor(I(0)).rot.y = world_yaw(F(1));
            return true;
        case 139: if (!slot_ok(I(0)) || n < 4) return false; { const EvVec3 r = actor(I(0)).rot; a.set_float(1, r.x); a.set_float(2, r.y - world_rot.y); a.set_float(3, r.z); } return true;
        case 140: case 142: case 144: case 150: case 151: case 149: {               // per-actor flags: (flag, chara...)
            const int flag = I(0);
            for (int i = 1; i < n; ++i) {
                const int s = I(i);
                if (!slot_ok(s)) continue;
                EvActor& ac = actor(s);
                if (id == 140) ac.draw = flag != 0;
                else if (id == 142) ac.collide = flag != 0;
                else if (id == 144) ac.shadow = flag != 0;
                else if (id == 150) ac.stop = flag != 0;
                else if (id == 151) ac.draw_before = flag != 0;
                else ac.foot_sound = flag;
            }
            return true;
        }
        case 141: case 143: case 145: case 152: case 153: case 156: case 158: case 159: case 160: return true;   // cloth, floor ids, lights: presentation only
        case 146: {                                                                 // _SET_NPC_AMBIENT(chara [, a [, b, c, d [, e]]])
            if (!slot_ok(I(0))) return false;
            EvActor& ac = actor(I(0));
            if (n == 2 || n == 3) ac.ambient[0] = ac.ambient[1] = ac.ambient[2] = F(1);
            else if (n >= 4) { ac.ambient[0] = F(1); ac.ambient[1] = F(2); ac.ambient[2] = F(3); }
            if (n >= 5) ac.ambient[3] = F(4);
            return true;
        }
        case 147:                                                                   // _SET_NPC_SCALE
            if (!slot_ok(I(0))) return false;
            actor(I(0)).scale = n >= 4 ? V3(1) : EvVec3{F(1), F(1), F(1)};
            return true;
        case 148: if (!slot_ok(I(0))) return false; put3(1, actor(I(0)).scale); return true;
        case 154: {                                                                 // _SET_NPC_BODY_SIZE
            if (!slot_ok(I(0))) return false;
            EvActor& ac = actor(I(0));
            ac.body[0] = F(1);
            if (n > 2) ac.body[1] = F(2);
            if (n > 3) ac.body[2] = F(3);
            return true;
        }
        case 155: if (!slot_ok(I(0))) return false; for (int k = 0; k < 3 && k + 1 < n; ++k) a.set_float(k + 1, actor(I(0)).body[k]); return true;
        // ---- messages
        case 192: {                                                                 // _MES_MAKE(win, id [, number [, names...]])
            MesWindow* w = win(I(0));
            if (!w || !w->bank) return false;
            std::vector<int> names(4, -1);
            for (int i = 3; i < n; ++i) { if (size_t(i - 3) >= names.size()) names.resize(size_t(i - 3) + 1, -1); names[size_t(i - 3)] = I(i); }
            w->make(I(1), n > 2 ? I(2) : 0, names, {});
            return true;
        }
        case 193: if (MesWindow* w = win(I(0))) { w->close(); return true; } return false;                // _MES_CLOSE
        case 194: if (MesWindow* w = win(I(0))) { w->next_page(); return true; } return false;            // _MES_NEXTPAGE
        case 195: {                                                                 // _SET_MES_AUTOSET
            MesWindow* w = win(I(0));
            if (!w) return false;
            if (n >= 5) { w->autoset_rect = true; for (int k = 0; k < 4; ++k) w->rect[k] = I(1 + k); }
            else { w->autoset_rect = false; w->autoset_a = I(1); w->autoset_b = I(2); }
            return true;
        }
        case 196: if (MesWindow* w = win(I(0))) { w->tail = I(1); if (n > 2) w->tail_w[1] = I(2); if (n > 3) w->tail_w[0] = I(3); return true; } return false;
        case 197: if (MesWindow* w = win(I(0))) { w->pos_mode = I(1); return true; } return false;       // _SET_MES_POS
        case 198: if (MesWindow* w = win(I(0))) { w->speed = F(1); return true; } return false;          // _SET_MES_DRAWSPEED
        case 199: if (MesWindow* w = win(I(0))) { w->cursor = I(1); return true; } return false;         // _SET_MES_CURSOR
        case 200: if (MesWindow* w = win(I(0))) { if (n >= 2) a.set_int(1, w->state() == MesWindow::Complete); return true; } return false;
        case 201: if (MesWindow* w = win(I(0))) { if (n >= 2) a.set_int(1, w->state() == MesWindow::PageWait); return true; } return false;
        case 202: if (MesWindow* w = win(I(0))) { if (n >= 2) a.set_int(1, w->state() == MesWindow::Closed); return true; } return false;
        case 203: if (MesWindow* w = win(I(0))) { w->okuri = I(1) != 0; return true; } return false;
        case 204: if (MesWindow* w = win(I(0))) { w->fukidashi = I(1); return true; } return false;
        case 205: if (MesWindow* w = win(I(0))) { w->win_flag = I(1); return true; } return false;
        case 206: {                                                                 // _ITEM_GET_MES(item [, kind])
            const int item = I(0);
            system_message.active = true;
            system_message.text = "You got " + (items ? system_bank.get(items->name_message(item), hero_names) : item_text_id_name(system_bank, item)) + "!";
            host_->item_got(item);
            return true;
        }
        case 207: system_message.active = false; system_message.text.clear(); return true;           // _INIT_SYS_MES
        case 208: case 209: system_message.active = true; return true;
        // ---- Georama / village state
        case 256: if (n >= 2) a.set_int(1, host_->georama_complete(I(0))); return true;               // _CHECK_COMPLETE_PARTS
        case 257: {                                                                 // _GET_EDIT_PARTS_POS
            EvVec3 p, r;
            if (n < 4 || !host_->georama_parts_pos(I(0), p, r)) return false;
            put3(1, from_world(p));
            if (n >= 7) put3(4, r);
            return true;
        }
        case 258: if (n >= 1) a.set_int(0, time_bucket()); return true;             // _GET_NOW_TIME
        case 259: if (n >= 2) a.set_int(1, host_->georama_cmp_event(I(0))); return true;
        case 260: host_->georama_set_cmp_event(I(0), I(1)); return true;
        case 261: { const float h = F(0); if (save && h >= 0.0f && h <= 24.0f) save->set_now_time(convert_time(h)); return true; }   // _SET_CLOCK
        case 262: if (n >= 1) a.set_float(0, invert_time(save ? save->now_time() : 0.0f)); return true;
        case 263: if (n >= 1) a.set_int(0, interior_flag); return true;             // _CHECK_PLACE
        case 264: if (n >= 2) a.set_int(1, host_->georama_request_percent(I(0))); return true;
        case 265: if (n >= 2) a.set_int(1, host_->georama_atra_parts(I(0))); return true;
        case 266: if (n >= 1 && save) a.set_int(0, save->day()); return true;       // _SGET_DAY
        case 267: if (n >= 2) a.set_int(1, host_->villager_present(I(0))); return true;
        case 268: if (n >= 2) a.set_int(1, host_->georama_request(I(0))); return true;                // _SCHECK_REQUEST
        case 269: return true;
        // ---- object handles
        case 320: case 321: case 322: case 330: case 336: {
            const int h = I(0);
            if (h < 0 || h >= 32) return false;
            EvHandle& hd = handles[size_t(h)];
            hd = EvHandle();
            hd.kind = id == 320 ? 1 : id == 321 ? 2 : id == 322 ? 3 : id == 330 ? 4 : 5;
            hd.index = I(1);
            hd.frame = n > 2 && id != 336 ? a.get_string(2) : "";
            return true;
        }
        case 323: {                                                                 // _OBJ_DRAW(flag, handles...)
            for (int i = 1; i < n; ++i) if (I(i) >= 0 && I(i) < 32) handles[size_t(I(i))].draw = I(0) != 0;
            return true;
        }
        case 324: if (I(0) >= 0 && I(0) < 32) { handles[size_t(I(0))].pos = W3(1); handles[size_t(I(0))].moved = true; handles[size_t(I(0))].pos_set = true; } return true;
        case 325: if (I(0) >= 0 && I(0) < 32 && n >= 4) put3(1, from_world(handles[size_t(I(0))].pos)); return true;
        case 326: if (I(0) >= 0 && I(0) < 32) { const EvVec3 r = V3(1); handles[size_t(I(0))].rot = {r.x, world_yaw(r.y), r.z}; handles[size_t(I(0))].moved = true; handles[size_t(I(0))].rot_set = true; } return true;
        case 327: if (I(0) >= 0 && I(0) < 32 && n >= 4) { const EvVec3 r = handles[size_t(I(0))].rot; a.set_float(1, r.x); a.set_float(2, r.y - world_rot.y); a.set_float(3, r.z); } return true;
        case 328: if (I(0) >= 0 && I(0) < 32) handles[size_t(I(0))].scale = V3(1); return true;
        case 329: if (I(0) >= 0 && I(0) < 32 && n >= 4) put3(1, handles[size_t(I(0))].scale); return true;
        case 332: {                                                                 // _HOBJ_BT_HOBJ(handle, dungeon object handles ...)
            const int h = I(0);
            if (h < 0 || h >= 32) return false;
            EvHandle& hd = handles[size_t(h)];
            hd = EvHandle();
            hd.kind = 6;
            for (int i = 1; i < n && i < 12; ++i) hd.bt.push_back(I(i));
            return true;
        }
        case 333: case 334: case 335: return true;
        // ---- talk
        case 384: if (n >= 1) a.set_int(0, talk_npc_info); return true;             // _GET_TALKNPC_INFO_ID
        case 385: if (n >= 1) a.set_int(0, talk_npc_info); return true;
        case 386: return true;
        case 387:                                                                   // _SET_TALK_MES(ids...)
            talk_list.clear();
            for (int i = 0; i < n; ++i) talk_list.push_back(I(i));
            return true;
        case 388: talk_kind = I(0); talk_header = I(1); return true;                // _SET_TALK_SELECT_MES
        case 389: if (n >= 1) a.set_int(0, 0); return true;
        case 390: return true;
        // ---- camera
        case 416: {                                                                 // _SET_CAMERA(pos, ref, speed)
            camera.follow_chara = -2;
            camera.cs.follow_on = false;
            const EvVec3 p = W3(0), r = W3(3);
            camera.cs.next_pos[0] = p.x; camera.cs.next_pos[1] = p.y; camera.cs.next_pos[2] = p.z;
            camera.cs.next_ref[0] = r.x; camera.cs.next_ref[1] = r.y; camera.cs.next_ref[2] = r.z;
            camera.cs.speed = F(6) <= 1.0f ? 1.0f : F(6);
            if (F(6) <= 1.0f) camera_step(camera.cs, 1);
            return true;
        }
        case 417: {                                                                 // _SET_FOLLOW_CAMERA
            CameraState& s = camera.cs;
            s.follow_on = true;
            if (n >= 7) {
                const EvVec3 p = W3(0);
                camera.follow_chara = -2;
                s.follow[0] = p.x; s.follow[1] = p.y; s.follow[2] = p.z;
                s.height = F(3); s.dist = F(4); s.angle = world_yaw(F(5)); s.speed = F(6) <= 0 ? 1.0f : F(6);
            } else {
                camera.follow_chara = I(0);
                s.height = F(1); s.dist = F(2); s.angle = world_yaw(F(3)); s.speed = F(4) <= 0 ? 1.0f : F(4);
                if (slot_ok(I(0))) {
                    const EvActor& ac = actor(I(0));
                    s.follow[0] = ac.pos.x; s.follow[1] = ac.pos.y + ac.body[1] * 0.8f; s.follow[2] = ac.pos.z;
                }
            }
            if (s.speed <= 1.0f) { camera_step(s, 1); }
            return true;
        }
        case 418: camera.cs.follow_on = true; camera.cs.angle += F(0); return true;
        case 419: camera.cs.height += F(0); return true;
        case 420: camera.cs.dist += F(0); return true;
        case 421: camera_step(camera.cs, 1); return true;
        case 422: {                                                                 // _SET_CAMERA_POS
            camera.follow_chara = -2;
            camera.cs.follow_on = false;
            const EvVec3 p = W3(0);
            camera.cs.next_pos[0] = p.x; camera.cs.next_pos[1] = p.y; camera.cs.next_pos[2] = p.z;
            return true;
        }
        case 424: {                                                                 // _SET_CAMERA_REF
            camera.follow_chara = -2;
            camera.cs.follow_on = false;
            const EvVec3 p = W3(0);
            camera.cs.next_ref[0] = p.x; camera.cs.next_ref[1] = p.y; camera.cs.next_ref[2] = p.z;
            return true;
        }
        case 423: if (n >= 3) put3(0, from_world({camera.cs.pos[0], camera.cs.pos[1], camera.cs.pos[2]})); return true;
        case 425: if (n >= 3) put3(0, from_world({camera.cs.ref[0], camera.cs.ref[1], camera.cs.ref[2]})); return true;
        case 426: {                                                                 // _SET_CAMERA_SPEED(int)
            const float sp = float(I(0));
            camera.cs.speed = sp <= 1.0f ? 1.0f : sp;
            if (sp <= 1.0f) {   // a speed of 0 / 1 is "jump": the next position is taken at once
                for (int k = 0; k < 3; ++k) { camera.cs.pos[k] = camera.cs.next_pos[k]; camera.cs.ref[k] = camera.cs.next_ref[k]; }
            }
            return true;
        }
        case 427: camera.projection = F(0); return true;
        case 428: return true;
        case 429: {                                                                 // _SET_CAMERA_ANGLE(yaw [, pitch]): aim the camera at yaw / pitch from its position
            CameraState& s = camera.cs;
            camera.follow_chara = -2;
            s.follow_on = false;
            const float yaw = world_yaw(F(0)), pitch = n > 1 ? F(1) : 0.0f;
            const float dx = s.next_ref[0] - s.next_pos[0], dy = s.next_ref[1] - s.next_pos[1], dz = s.next_ref[2] - s.next_pos[2];
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
            s.next_ref[0] = s.next_pos[0] + d * std::sin(yaw) * std::cos(pitch);
            s.next_ref[1] = s.next_pos[1] + d * std::sin(pitch);
            s.next_ref[2] = s.next_pos[2] + d * std::cos(yaw) * std::cos(pitch);
            return true;
        }
        case 430: {
            const CameraState& s = camera.cs;
            if (n >= 1) a.set_float(0, std::atan2(s.ref[0] - s.pos[0], s.ref[2] - s.pos[2]) - world_rot.y);
            if (n >= 2) { const float h = std::hypot(s.ref[0] - s.pos[0], s.ref[2] - s.pos[2]); a.set_float(1, std::atan2(s.ref[1] - s.pos[1], h)); }
            return true;
        }
        case 431: {                                                                 // _SET_CAMERA_ROTATE(yaw): orbit the camera about the reference
            CameraState& s = camera.cs;
            camera.follow_chara = -2;
            s.follow_on = false;
            const float yaw = world_yaw(F(0));
            const float h = std::hypot(s.next_pos[0] - s.next_ref[0], s.next_pos[2] - s.next_ref[2]);
            s.next_pos[0] = s.next_ref[0] + h * std::sin(yaw);
            s.next_pos[2] = s.next_ref[2] + h * std::cos(yaw);
            return true;
        }
        case 432: if (n >= 1) a.set_float(0, std::atan2(camera.cs.pos[0] - camera.cs.ref[0], camera.cs.pos[2] - camera.cs.ref[2]) - world_rot.y); return true;
        case 433: camera.reset_mode = I(0); camera.reset_angle = 3.14159f; return true;               // _RESET_CAMERA
        case 436: camera.reset_mode = 1; camera.reset_angle = F(0); return true;                      // _RESET_CAMERA_ANGLE
        case 434:                                                                   // _SYNC_CAMERA_REF_CHARA
            camera.sync_ref_chara = I(0);
            camera.sync_ref_offset = n >= 4 ? V3(1) : EvVec3();
            return true;
        case 435: camera.sync_ref_chara = -2; return true;
        case 437: camera.sync_ref_obj = I(0); camera.sync_obj_offset = n >= 4 ? V3(1) : EvVec3(); return true;
        case 438: camera.sync_ref_obj = -1; return true;
        case 439: camera.sync_pos_obj = I(0); return true;
        case 440: camera.sync_pos_obj = -1; return true;
        case 441: camera.roll = F(0); return true;
        case 442: if (n >= 1) a.set_float(0, camera.roll); return true;
        // ---- fades, screen
        case 500: case 501: {                                                       // _FADE_IN / _FADE_OUT(frames [, r,g,b])
            const bool out = id == 501;
            const int frames_n = I(0);
            fade.out = out;
            fade.r = I(1); fade.g = I(2); fade.b = I(3);
            if (frames_n < 0) {
                fade.alpha = out ? 128.0f : 0.0f;
                fade.active = false;
                fade.end = true;
            } else {
                fade.alpha = out ? 0.0f : 128.0f;   // EdFadeIn / EdFadeOut: the overlay starts fully on / fully off
                fade.step = frames_n > 0 ? 128.0f / float(frames_n) : 128.0f;
                fade.active = true;
                fade.end = false;
            }
            return true;
        }
        case 502: if (n >= 1) a.set_int(0, fade.end ? 1 : 0); return true;          // _CHECK_FADE
        case 503: draw_sprites.clear(); return true;                                // _INIT_SPRITE
        case 504: case 516: {                                                       // _DRAW_SPRITE / _DRAW_BG_SPRITE(x, y, "tex", u, v, w, h)
            EvSprite sp;
            sp.active = sp.show = true;
            sp.background = id == 516;
            sp.x = F(0); sp.y = F(1);
            sp.tex = n > 2 ? a.get_string(2) : "";
            sp.u = I(3); sp.v = I(4); sp.w = I(5); sp.h = I(6);
            draw_sprites.push_back(std::move(sp));
            return true;
        }
        case 505: back_on = I(0) == 0; if (n >= 4) { back[0] = F(1); back[1] = F(2); back[2] = F(3); } return true;   // _DRAW_BACK
        case 506: shadows_on = I(0) == 0; return true;                              // _DRAW_SHADOW
        case 507: case 508: case 509: return true;                                  // clip sphere, water drawing flags
        case 510:                                                                   // _SCREEN_FILTER(r,g,b,a)
            if (n >= 4 && F(3) >= 0.0f && F(0) >= 0.0f) { filter_on = true; for (int k = 0; k < 4; ++k) filter[k] = F(k); }
            else filter_on = false;
            return true;
        case 511: {                                                                 // _SET_LIGHT(idx, dx,dy,dz [, r,g,b])
            const int k = I(0);
            if (k < 0 || k > 3) return false;
            EvLight& l = lights[size_t(k)];
            l.on = true;
            for (int j = 0; j < 3; ++j) l.dir[j] = F(1 + j);
            if (n >= 7) for (int j = 0; j < 3; ++j) l.color[j] = F(4 + j);
            return true;
        }
        case 512: { const int k = I(0); if (k < 0 || k > 3) return false; for (int j = 0; j < 3; ++j) lights[size_t(k)].color[j] = F(1 + j); return true; }
        case 513: ambient_on = true; for (int j = 0; j < 3; ++j) ambient[j] = F(j); return true;   // _SET_AMBIENT
        case 514: return true;
        case 515: for (EvLight& l : lights) l.on = false; ambient_on = false; return true;       // _LOAD_LIGHT: back to the map's own light
        case 517: dof_level = I(0); return true;
        case 518: {                                                                 // _SP_INIT(sprite or -1)
            if (I(0) < 0) { for (EvSprite& s : sprites) s = EvSprite(); return true; }
            if (I(0) < 16) { sprites[size_t(I(0))] = EvSprite(); return true; }
            return false;
        }
        case 519: {                                                                 // _SP_SET_TEX(sprite, "tex", u,v,w,h [,bilinear])
            if (I(0) < 0 || I(0) >= 16) return false;
            EvSprite& s = sprites[size_t(I(0))];
            s.active = true;
            s.tex = n > 1 ? a.get_string(1) : "";
            s.u = I(2); s.v = I(3); s.w = I(4); s.h = I(5);
            s.bilinear = n > 6 && I(6) != 0;
            return true;
        }
        case 520: {                                                                 // _SP_SET_POS(sprite, x,y [,w,h])
            if (I(0) < 0 || I(0) >= 16) return false;
            EvSprite& s = sprites[size_t(I(0))];
            s.x = F(1); s.y = F(2);
            if (n >= 5) { s.sw = F(3); s.sh = F(4); }
            return true;
        }
        case 521: {                                                                 // _SP_SET_RGBA(sprite, r,g,b [,a])
            if (I(0) < 0 || I(0) >= 16) return false;
            EvSprite& s = sprites[size_t(I(0))];
            s.rgba[0] = F(1); s.rgba[1] = F(2); s.rgba[2] = F(3);
            s.rgba[3] = n > 4 ? F(4) : 128.0f;
            return true;
        }
        case 522: { if (I(0) < 0 || I(0) >= 16) return false; EvSprite& s = sprites[size_t(I(0))]; s.rot_cx = F(1); s.rot_cy = F(2); s.rot = F(3); return true; }
        case 523: for (int i = 1; i < n; ++i) if (I(i) >= 0 && I(i) < 16) sprites[size_t(I(i))].show = I(0) != 0; return true;   // _SP_DRAW(show, sprites...)
        case 526: if (I(0) >= 0 && I(0) < 16) { sprites[size_t(I(0))].vx = F(1); sprites[size_t(I(0))].vy = F(2); return true; } return false;
        case 527: return true;
        case 528: thunder = I(0) != 0; return true;
        case 529: for (int k = 0; k < 4; ++k) wind[k] = F(k); return true;
        case 524: day_banner = false; return true;
        case 525: day_banner = true; return true;
        // ---- action sequences
        case 600: {                                                                 // _ASQ_INIT
            if (I(0) < 0) { for (ActionSeq& q : asq) q.clear(); return true; }
            if (ActionSeq* q = chan(I(0))) { q->clear(); return true; }
            return false;
        }
        case 601: {                                                                 // _ASQ_SYNC_CHARA(chan, chara)
            ActionSeq* q = chan(I(0));
            if (!q || !slot_ok(I(1))) return false;
            q->chara = I(1);
            const EvActor& ac = actor(I(1));
            AsqNode p;
            p.type = kAsqSetPos;
            p.v[0] = ac.pos.x; p.v[1] = ac.pos.y; p.v[2] = ac.pos.z;
            for (int k = 0; k < 4; ++k) q->last_target[k] = p.v[k];
            q->move.push_back(p);
            AsqNode r;
            r.type = kAsqSetRot;
            r.v[0] = ac.rot.x; r.v[1] = ac.rot.y; r.v[2] = ac.rot.z;
            q->move.push_back(r);
            return true;
        }
        case 602: {                                                                 // _ASQ_SET_POS
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            AsqNode p;
            p.type = kAsqSetPos;
            const EvVec3 w = W3(1);
            p.v[0] = w.x; p.v[1] = w.y; p.v[2] = w.z;
            for (int k = 0; k < 4; ++k) q->last_target[k] = p.v[k];
            q->move.push_back(p);
            return true;
        }
        case 603: {                                                                 // _ASQ_MOVE(chan, x,y,z, ticks) / (chan, ticks)
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            AsqNode m;
            m.type = kAsqMove;
            if (n >= 5) { const EvVec3 w = W3(1); m.v[0] = w.x; m.v[1] = w.y; m.v[2] = w.z; m.value = I(4); }
            else { for (int k = 0; k < 4; ++k) m.v[k] = q->last_target[k]; m.value = I(1); }
            for (int k = 0; k < 4; ++k) q->last_target[k] = m.v[k];
            q->move.push_back(m);
            return true;
        }
        case 604: {                                                                 // _ASQ_MOVE_STEP(chan, x,y,z, speed)
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            const EvVec3 w = W3(1);
            AsqNode m;
            m.type = kAsqMove;
            m.v[0] = w.x; m.v[1] = w.y; m.v[2] = w.z;
            const float d = dist3(m.v, q->last_target);
            const float sp = F(4);
            int ticks = sp != 0.0f ? int(d / sp) : 0;
            if (sp != 0.0f && d / sp - float(ticks) > 0.0f) ++ticks;
            m.value = ticks;
            for (int k = 0; k < 4; ++k) q->last_target[k] = m.v[k];
            q->move.push_back(m);
            return true;
        }
        case 605: {                                                                 // _ASQ_ROT_REF(chan, x,y,z, speed)
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            AsqNode m;
            m.type = kAsqRotRef;
            const EvVec3 w = W3(1);
            m.v[0] = w.x; m.v[1] = w.y; m.v[2] = w.z; m.v[3] = F(4);
            q->move.push_back(m);
            return true;
        }
        case 606: if (ActionSeq* q = chan(I(0))) { AsqNode m; m.type = kAsqClearRot; q->move.push_back(m); return true; } return false;
        case 607: if (ActionSeq* q = chan(I(0))) { AsqNode m; m.type = kAsqWaitRot; q->move.push_back(m); return true; } return false;
        case 608: if (ActionSeq* q = chan(I(0))) { AsqNode m; m.type = kAsqRotMove; m.v[3] = F(1); q->move.push_back(m); return true; } return false;
        case 609: {                                                                 // _ASQ_SET_ROT(chan, rx,ry,rz) / (chan, yaw)
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            AsqNode m;
            m.type = kAsqSetRot;
            if (n >= 4) { m.v[0] = F(1); m.v[1] = world_yaw(F(2)); m.v[2] = F(3); }
            else { m.v[1] = world_yaw(F(1)); }
            q->move.push_back(m);
            return true;
        }
        case 610: if (ActionSeq* q = chan(I(0))) { AsqNode m; m.type = kAsqDelayRot; m.value = I(1); q->move.push_back(m); return true; } return false;
        case 611: {                                                                 // _ASQ_CHECK(chan, &out): 1 while commands are pending
            ActionSeq* q = chan(I(0));
            if (!q || n < 2) return false;
            a.set_int(1, q->idle() ? 0 : 1);
            return true;
        }
        case 612: if (ActionSeq* q = chan(I(0))) { AsqNode m; m.type = kAsqMotionTrg; m.value = n > 1 ? I(1) : 0; q->move.push_back(m); return true; } return false;
        case 616: if (ActionSeq* q = chan(I(0))) { AsqNode m; m.type = kAsqAnimeTrg; m.value = n > 1 ? I(1) : 0; q->move.push_back(m); return true; } return false;
        case 613: case 614: case 615: {                                             // _ASQ_MOTION_PLAY / STOP / NEXT(chan, motion [, speed [, flags]])
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            AsqNode m;
            m.type = kAsqMotion;
            m.a = I(1);
            m.kind = id == 613 ? 5 : id == 614 ? 6 : 7;
            m.speed = n > 2 ? F(2) : -1.0f;
            m.mflags = n > 3 ? (I(3) & 4) : 0;
            q->motion.push_back(m);
            return true;
        }
        case 617: {                                                                 // _ASQ_ANIME(chan, anime [, on [, frames [, off_after]]])
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            AsqNode m;
            m.type = kAsqAnime;
            m.a = I(1);
            m.b = n > 2 ? I(2) : 1;
            m.value = n > 3 ? I(3) : 0;
            m.speed = (m.value > 0 && n > 4) ? float(I(4)) : 0.0f;
            q->anime.push_back(m);
            return true;
        }
        case 618: {                                                                 // _ASQ_ROT_ANGLE(chan, angle, speed)
            ActionSeq* q = chan(I(0));
            if (!q) return false;
            AsqNode m;
            m.type = kAsqRotAngle;
            m.v[1] = world_yaw(F(1));
            m.v[3] = F(2);
            q->move.push_back(m);
            return true;
        }
        case 650:                                                                   // _OBJ_ANIME_INIT(slots ...): -1 or no argument clears all
            if (n == 0) for (ObjAnime& q : obj_anime) q.active = false;
            for (int i = 0; i < n; ++i) {
                if (I(i) < 0) for (ObjAnime& q : obj_anime) q.active = false;
                else if (I(i) < 16) obj_anime[size_t(I(i))].active = false;
            }
            return true;
        case 651: {                                                                 // _OBJ_ANIME(slot, handle, kind, mode, speed[3] [, start[3] [, end[3]]])
            if (I(0) < 0 || I(0) >= 16) return true;
            ObjAnime& q = obj_anime[size_t(I(0))];
            q = ObjAnime();
            q.handle = I(1);
            q.kind = I(2);
            q.mode = I(3);
            for (int k = 0; k < 3; ++k) {
                q.speed[k] = F(4 + k);
                q.start[k] = n > 9 ? F(7 + k) : 0.0f;
                q.end[k] = n > 12 ? F(10 + k) : 0.0f;
                q.cur[k] = q.start[k];
            }
            q.active = true;
            return true;
        }
        // ---- save data and items
        case 700: if (n == 2 && save) { save->set_game_flag(I(0), I(1) != 0); return true; } return false;       // _SSET_GAME_FLAG
        case 701: if (n == 2 && save) { a.set_int(1, save->game_flag(I(0)) ? 1 : 0); return true; } return false;
        case 702: {                                                                 // _SITEM_GET(item)
            if (!status || !items || !rng) return false;
            if (check_item_get(*status, I(0)) == 0) get_item(*status, *items, I(0), *rng);
            return true;
        }
        case 703: if (save) save->set_map_flag(map_no, I(0), I(1) == 0); return true;                          // _SSET_MAP_FLAG (inverted)
        case 704: if (n >= 2 && save) { a.set_int(1, save->map_flag(map_no, I(0)) ? 0 : 1); return true; } return false;
        case 705: if (I(0) >= 0 && I(0) < 32) work_flags[I(0)] = I(1); return true;
        case 706: if (n >= 2) a.set_int(1, I(0) >= 0 && I(0) < 32 ? work_flags[I(0)] : 0); return true;
        case 707: {                                                                 // _SITEM_CHECK(item, &out): bag slot or -1
            if (n < 2 || !status) return false;
            int slot = -1;
            for (int i = 0; i < kBagSlots; ++i)
                if (status->get<int16_t>(0x436e + size_t(i) * 2) == I(0)) { slot = i; break; }
            a.set_int(1, slot);
            return true;
        }
        case 724: {                                                                 // _SITEM_CHECK_ALL(item, &out)
            if (n < 2 || !status) return false;
            int found = 0;
            for (int i = 0; i < kBagSlots; ++i)
                if (status->get<int16_t>(0x436e + size_t(i) * 2) == I(0)) found = 1;
            a.set_int(1, found);
            return true;
        }
        case 718: {                                                                 // _SITEM_LOST(item)
            if (!status) return false;
            for (int i = 0; i < kBagSlots; ++i)
                if (status->get<int16_t>(0x436e + size_t(i) * 2) == I(0)) { consume_bag_slot(*status, i); break; }
            return true;
        }
        case 708: {                                                                 // _SGET_DUNGEON_STATUS(dungeon, &quest, &deepest)
            if (n < 3 || !status) return false;
            const int d = I(0);
            a.set_int(1, 0);
            a.set_int(2, d >= 0 && d < 7 ? status->get<int8_t>(0x42bf + size_t(d)) : -1);
            return true;
        }
        case 709: if (n >= 1 && status) a.set_int(0, status->get<int8_t>(5)); return true;
        case 710: if (status) status->set<int8_t>(5, int8_t(I(0))); return true;
        case 711: if (save) save->set_game_int(I(0), I(1)); return true;
        case 712: if (n >= 2 && save) { a.set_int(1, save->game_int(I(0))); return true; } return false;
        case 713: host_->georama_set_request_flag(I(0), I(1)); return true;
        case 719: host_->georama_atra_get(0, I(0), I(1)); return true;
        case 720: host_->georama_atra_get(1, I(0), I(1)); return true;
        case 714: case 715: case 717: return true;
        case 716: if (n >= 2 && status) { a.set_int(1, status->weapon(std::clamp(I(0), 0, 5)).id()); } return true;
        case 721: if (save) save->set_now_time(F(0)); return true;
        case 722: if (n >= 1 && save) a.set_float(0, save->now_time()); return true;
        case 723: if (save) save->add_now_time(F(0)); return true;
        case 725: return true;
        // ---- sound
        case 800: if (n >= 1) a.set_int(0, host_->sound_loading() ? 1 : 0); return true;
        case 806: if (n >= 1) a.set_int(0, host_->bgm_playing()); return true;
        case 819: if (n >= 1) a.set_int(0, host_->bgm_fading() ? 1 : 0); return true;
        case 801: case 802: case 803: case 804: case 807: case 808: case 810: case 811: case 812: case 813: case 815: case 816: case 817: case 818: case 820: case 821:
        case 822: case 823: case 824: case 825: case 826: {
            float v[6] = {0, 0, 0, 0, 0, 0};
            for (int i = 0; i < std::min(n, 6); ++i) v[i] = F(i);
            host_->sound(ev_ext_name(id), I(0), n > 1 ? I(1) : 0, v);
            return true;
        }
        case 805: if (n >= 1) a.set_int(0, 128); return true;
        case 814: if (n >= 1) a.set_int(0, -1); return true;
        case 809: return true;
        // ---- fishing
        case 999: if (n >= 2) host_->load_main_chara(a.get_string(0), a.get_string(1), n > 2 ? I(2) : 0); return true;   // _LOAD_MAIN_CHARA("model.chr", "info.cfg", mode)
        case 994: fishing.bait_item = I(0); fishing.bait_changed = true; return true;   // _SET_FISHING_ESA(item)
        case 995: fishing.exit_requested = true; return true;                           // _EXIT_FISHING
        case 996: fishing.area[0] = F(0); fishing.area[1] = F(1); fishing.area[2] = F(2); fishing.area[3] = F(3); return true;   // _INIT_FISH(x0, z0, x1, z1)
        case 997:                                                                        // _GOTO_FISHING
            nav_.kind = EvNav::Fishing;
            result_code_ = 0xb;
            return true;
        case 998:                                                                        // _LOAD_FISHING_DATA(set, x0, z0, x1, z1, water, ground)
            fishing.loaded = true;
            fishing.pond_set = I(0);
            fishing.rect[0] = F(1); fishing.rect[1] = F(2); fishing.rect[2] = F(3); fishing.rect[3] = F(4);
            fishing.water = F(5);
            fishing.ground = n > 6 ? F(6) : -30.0f;
            return true;
        // ---- the EB mini-game (event battles): not played; the loops report that they are finished
        case 950: eb.init(F(0)); return true;                                                    // _EB_INIT(speed)
        case 951: eb.set_key(F(0), uint32_t(I(1)), n > 2 ? I(2) : 0); return true;                // _EB_SET_KEY(motion time, pad mask [, level])
        case 953: {                                                                              // _EB_SET_MOTION(actor, motion, motion, ...)
            std::vector<EbMotion> list;
            bool ok = true;
            for (int k = 1; k < n && ok; ++k) {
                const int m = I(k);
                if (m < 0) break;
                EbMotion e;
                e.no = m;
                const bool found = host_->actor_motion_range(I(0), m, e.start, e.end, e.speed);
                eb.log.push_back("motion slot " + std::to_string(I(0)) + " no " + std::to_string(m) + (found ? " found " + std::to_string(e.start) + ".." + std::to_string(e.end) + " x" + std::to_string(e.speed) : std::string(" MISSING")));
                if (!found || e.speed <= 0.0f) ok = false;
                list.push_back(e);
            }
            if (ok) eb.set_motions(list);
            return true;
        }
        case 954: return true;                                                                   // (debug)
        case 955: eb.intro_start(); return true;                                                 // _EB_INTRO_START
        case 956: if (n >= 1) a.set_int(0, eb.intro_step()); eb_sounds(); return true;           // _EB_INTRO_LOOP(&done)
        case 957: eb.exit(); return true;                                                        // _EB_INTRO_END
        case 958: eb.fade_bgm = I(0) != 0; eb.play_fanfare = n > 1 ? I(1) != 0 : false; return true;   // _EB_FINISH_SOUND(fade, fanfare)
        case 952: if (n >= 1) a.set_int(0, eb.step(pad_down)); eb_sounds(); return true;         // _EB_LOOP(&result): the raw pad word (no OK / cancel swap)
        // ---- variable arguments of system events
        case 900: case 901: case 902: return true;
        default: break;
    }
    if (unknown_ext_ids.insert(id).second) host_->unknown_ext(id, ev_ext_name(id));
    return true;
}

}  // namespace dccore
