#include "dccore/opening.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "dccore/mds.h"
#include "dccore/pack.h"

namespace dccore {

namespace {

constexpr uint32_t kTitleBase = 0x01DABD00u;
constexpr uint32_t kSneTable = 0x01DD5820u;

struct CmdDef { const char* name; OpKind kind; int args; };
// the game's table order (prefix matching: the longer names come first)
const CmdDef kDefs[] = {
    {"OBJ_MOTION2", OpKind::ObjMotion2, 3}, {"OBJ_MOTION", OpKind::ObjMotion, 2}, {"OBJ_MOVE", OpKind::ObjMove, 2}, {"OBJ_TALK", OpKind::ObjTalk, 7},
    {"OBJ_EYE", OpKind::ObjEye, 3}, {"OBJ_MOUTH", OpKind::ObjMouth, 3}, {"OBJ_DISP", OpKind::ObjDisp, 2}, {"OBJ_STEP", OpKind::ObjStep, 2},
    {"CAMERA", OpKind::Camera, 4}, {"SE_STOP", OpKind::SeStop, 3}, {"SE", OpKind::Se, 4}, {"SOUND_FADE", OpKind::SoundFade, 3}, {"FADE_IN", OpKind::FadeIn, 2},
    {"FADE_OUT", OpKind::FadeOut, 2}, {"MESSAGE", OpKind::Message, 6}, {"NEXT_MES", OpKind::NextMes, 1}, {"MES_CLR", OpKind::MesClr, 0},
    {"LOAD_OBJ", OpKind::LoadObj, 2}, {"SPRITE", OpKind::Sprite, 1}, {"BOM", OpKind::Bom, 4}, {"BEEM", OpKind::Beem, 6}, {"SCENE_LOAD", OpKind::SceneLoad, 1},
    {"SCENE", OpKind::Scene, 1}, {"END", OpKind::End, 0}, {"WAIT_KEY", OpKind::WaitKey, 1}, {"WAIT", OpKind::Wait, 1},
};

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

std::string cstr_at(const Bytes& d, uint32_t addr) {
    if (addr < kTitleBase || addr - kTitleBase >= d.size()) return {};
    const char* p = reinterpret_cast<const char*>(d.data()) + (addr - kTitleBase);
    return std::string(p, strnlen(p, 96));
}

uint32_t rd32(const Bytes& d, uint32_t addr) {
    uint32_t v = 0;
    if (addr >= kTitleBase && addr - kTitleBase + 4 <= d.size()) std::memcpy(&v, d.data() + (addr - kTitleBase), 4);
    return v;
}

float rdf(const Bytes& d, uint32_t addr) {
    const uint32_t v = rd32(d, addr);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

}  // namespace

std::vector<OpCommand> parse_opening_script(const std::string& text) {
    std::vector<OpCommand> out;
    size_t pos = 0;
    int line_no = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++line_no;
        const size_t cm = line.find("//");
        if (cm != std::string::npos) line.resize(cm);
        line = trim(line);
        if (line.empty()) continue;
        const CmdDef* def = nullptr;
        for (const CmdDef& d : kDefs)
            if (line.compare(0, std::strlen(d.name), d.name) == 0) { def = &d; break; }
        if (!def) continue;   // (the game would exit; a damaged line is skipped)
        OpCommand c;
        c.kind = def->kind;
        c.line = line_no;
        std::string rest = trim(line.substr(std::strlen(def->name)));
        size_t i = 0;
        size_t start = 0;
        while (start <= rest.size() && i < c.a.size() && !rest.empty()) {
            size_t comma = rest.find(',', start);
            if (comma == std::string::npos) comma = rest.size();
            const std::string tok = trim(rest.substr(start, comma - start));
            c.a[i++] = tok == "ON" ? 1.0f : tok == "OFF" ? 0.0f : float(std::atof(tok.c_str()));
            start = comma + 1;
            if (comma == rest.size()) break;
        }
        out.push_back(c);
    }
    return out;
}

SneClip load_sne(View data, const std::string& name) {
    SneClip s;
    s.name = name;
    Pack p{Bytes(data.begin(), data.end())};
    for (const PackEntry& e : p.entries()) {
        const std::string& n = e.name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".mds") == 0) {
            try {
                const Mds m = parse_mds(p.data(e), false);
                for (const Object& o : m.objects) {
                    s.node_names.push_back(o.name);
                    s.node_parent.push_back(o.parent);
                    s.node_matrix.push_back(o.matrix);
                }
            } catch (const std::exception&) {
            }
        } else if (n.size() > 4 && n.compare(n.size() - 4, 4, ".mot") == 0) {
            s.tracks = parse_mot(p.data(e));
        }
    }
    return s;
}

bool SneClip::camera_at(double t, float pos[3], float ref[3], float* fov) const { return sample_camera_tracks(tracks, t, pos, ref, fov); }

bool SneClip::node_at(const std::string& nm, double t, float pos[3], float* yaw) const {
    for (size_t i = 0; i < node_names.size(); ++i) {
        if (node_names[i] != nm) continue;
        const auto& m = node_matrix[i];
        pos[0] = m[12]; pos[1] = m[13]; pos[2] = m[14];
        for (const MotTrack& tr : tracks) {
            if (tr.type != 2 || tr.object != i || tr.keys.empty()) continue;
            const auto& ks = tr.keys;
            if (t <= ks.front().time) { std::memcpy(pos, ks.front().v, 12); break; }
            if (t >= ks.back().time) { std::memcpy(pos, ks.back().v, 12); break; }
            size_t hi = 1;
            while (hi < ks.size() && ks[hi].time <= t) ++hi;
            const MotKey &a = ks[hi - 1], &b = ks[hi];
            const float f = b.time != a.time ? float((t - a.time) / (b.time - a.time)) : 0.0f;
            for (int k = 0; k < 3; ++k) pos[k] = a.v[k] + (b.v[k] - a.v[k]) * f;
            break;
        }
        if (yaw) *yaw = std::atan2(m[8], m[10]);
        return true;
    }
    return false;
}

std::vector<OpStageEntry> opening_stage_table(const Bytes& d, uint32_t address, int count) {
    std::vector<OpStageEntry> out;
    for (int i = 0; i < count; ++i) {
        const uint32_t a = address + uint32_t(i) * 0x20;
        OpStageEntry e;
        e.model = cstr_at(d, rd32(d, a));
        e.shadow = cstr_at(d, rd32(d, a + 4));
        for (int k = 0; k < 3; ++k) {
            e.pos[k] = rdf(d, a + 8 + 4 * uint32_t(k)) * 10.0f;
            e.rot_deg[k] = rdf(d, a + 0x14 + 4 * uint32_t(k));
        }
        out.push_back(e);
    }
    return out;
}

std::vector<std::string> opening_sne_names(const Bytes& d) {
    std::vector<std::string> out;
    for (uint32_t a = kSneTable;; a += 8) {
        const std::string path = cstr_at(d, rd32(d, a));
        if (path.find("scene") == std::string::npos) break;
        out.push_back(path);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------

OpeningSim::OpeningSim(std::vector<OpCommand> cmds, std::vector<std::string> sne_names) : cmds_(std::move(cmds)), sne_names_(std::move(sne_names)) {
    for (OpObject& o : obj) o = OpObject{};
    obj[6].disp = obj[7].disp = true;   // the dance leader and the dancers are displayed from the start
}

void OpeningSim::press_ok() { wait_key_ = false; }
void OpeningSim::skip() { end_flag_ = true; }

void OpeningSim::execute(const OpCommand& c, bool& stop) {
    const auto& a = c.a;
    auto idx = [&](int k) { return std::clamp(int(a[size_t(k)]), 0, 22); };
    OpEvent ev;
    ev.a = a;
    switch (c.kind) {
        case OpKind::ObjMotion: { OpObject& o = obj[size_t(idx(0))]; o.motion = int(a[1]); o.motion2 = -1; ++o.motion_serial; break; }
        case OpKind::ObjMotion2: { OpObject& o = obj[size_t(idx(0))]; o.motion = int(a[1]); o.motion2 = int(a[2]); ++o.motion_serial; break; }
        case OpKind::ObjMove: obj[size_t(idx(0))].move = true; break;
        case OpKind::ObjDisp: obj[size_t(idx(0))].disp = a[1] != 0.0f; break;
        case OpKind::ObjStep: obj[size_t(idx(0))].step = a[1]; break;
        case OpKind::ObjEye: { OpObject& o = obj[size_t(idx(0))]; o.eye = int(a[1]); o.eye_time = a[2]; break; }
        case OpKind::ObjMouth: { OpObject& o = obj[size_t(idx(0))]; o.mouth = int(a[1]); o.mouth_time = a[2]; break; }
        case OpKind::ObjTalk: {
            OpObject& o = obj[size_t(idx(0))];
            o.talking = true;
            o.mouth_time = a[2];
            balloon.active = true;
            balloon.speaker = idx(0);
            balloon.msg = int(a[1]);
            balloon.life = a[2];
            balloon.x = a[3]; balloon.y = a[4]; balloon.tx = a[5]; balloon.ty = a[6];
            break;
        }
        case OpKind::Message:
            balloon.active = true;
            balloon.speaker = -1;
            balloon.msg = int(a[0]);
            balloon.life = a[1];
            balloon.x = a[2]; balloon.y = a[3]; balloon.tx = a[4]; balloon.ty = a[5];
            break;
        case OpKind::NextMes: balloon.page_limit = int(a[0]); break;
        case OpKind::MesClr:
            balloon.active = false;
            balloon.msg = 0;
            for (OpObject& o : obj) o.talking = false;
            break;
        case OpKind::Camera:
            ++cut;
            cam_key = int(a[0]);
            cam_start = a[1];
            cam_end = a[2];
            cam_speed = a[3];
            t_ = cam_start;
            clip_name_ = size_t(cut) < sne_names_.size() ? sne_names_[size_t(cut)] : std::string();
            ev.kind = OpEvent::Camera;
            events.push_back(ev);
            break;
        case OpKind::Se: ev.kind = OpEvent::Se; events.push_back(ev); break;
        case OpKind::SeStop: ev.kind = OpEvent::SeStop; events.push_back(ev); break;
        case OpKind::SoundFade: ev.kind = OpEvent::SoundFade; events.push_back(ev); break;
        case OpKind::FadeIn: fade_white = a[0] != 0.0f; fade_speed = -a[1]; break;
        case OpKind::FadeOut: fade_white = a[0] != 0.0f; fade_speed = a[1]; break;
        case OpKind::LoadObj: ev.kind = OpEvent::LoadObj; events.push_back(ev); break;
        case OpKind::Sprite: sprite_id = int(a[0]); sprite_on = a[0] != 0.0f; ev.kind = OpEvent::Sprite; events.push_back(ev); break;
        case OpKind::Bom: ev.kind = OpEvent::Bom; events.push_back(ev); break;
        case OpKind::Beem: ev.kind = OpEvent::Beem; events.push_back(ev); break;
        case OpKind::SceneLoad: ev.kind = OpEvent::SceneLoad; events.push_back(ev); break;
        case OpKind::Scene: scene = int(a[0]); ev.kind = OpEvent::Scene; events.push_back(ev); break;
        case OpKind::End: end_flag_ = true; stop = true; break;
        case OpKind::WaitKey: wait_key_ = true; stop = true; break;
        case OpKind::Wait: c_ = a[0] - 1.0f; stop = true; break;
    }
}

void OpeningSim::step() {
    events.clear();
    ++steps;
    if (end_flag_) return;
    if (auto_key && wait_key_ && ++key_steps_ >= 2) { wait_key_ = false; key_steps_ = 0; }
    // CScript::Step
    if (wait_key_) c_ = cam_speed + 1.0f;
    if (c_ < 1.0f) {
        bool stop = false;
        while (!stop && pc_ < cmds_.size()) execute(cmds_[pc_++], stop);
        if (pc_ >= cmds_.size() && !stop) end_flag_ = true;
    } else {
        c_ -= cam_speed;
    }
    // the SPRITE command with a value of OFF ends the effect (the argument says ON or OFF)
    // the camera clip: the time holds on the last frame and is frozen while the script waits for a key
    if (!wait_key_) {
        t_ = std::min(t_, cam_end - 1.0f);
        cam_time = t_;
        t_ += cam_speed;
    }
    // timers count in camera frames
    for (OpObject& o : obj) {
        if (o.eye_time > 0 && (o.eye_time -= cam_speed) <= 0) { o.eye_time = 0; o.eye = 0; }
        if (o.mouth_time > 0 && (o.mouth_time -= cam_speed) <= 0) { o.mouth_time = 0; o.mouth = 0; o.talking = false; }
    }
    if (balloon.active) {
        if (wait_key_) balloon.life = std::max(balloon.life, 2.0f * cam_speed);
        else if ((balloon.life -= cam_speed) <= 0) { balloon.active = false; balloon.msg = 0; }
    }
    // fades
    if (fade_speed != 0.0f) {
        fade_alpha = std::clamp(fade_alpha + fade_speed, 0.0f, 128.0f);
        if (fade_alpha <= 0.0f || fade_alpha >= 128.0f) fade_speed = 0.0f;
    }
}

}  // namespace dccore
