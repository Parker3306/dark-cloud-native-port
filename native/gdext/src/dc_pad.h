#pragma once
// The DualShock (any SDL game controller) as the game's own pad: the bits of the PS2 pad word the game reads (Sony libpad layout, the same one PCSX2 reports), the sticks
// scaled like the PCSX2 default pad profile (AxisScale 1.33). Cross / Circle / Square / Triangle are Godot's A / B / X / Y on a DualShock, as in the PCSX2 SDL bindings.
//   0x0001 L2   0x0002 R2   0x0004 L1   0x0008 R1   0x0010 Triangle   0x0020 Circle   0x0040 Cross   0x0080 Square
//   0x0100 Select   0x0200 L3   0x0400 R3   0x0800 Start   0x1000 Up   0x2000 Right   0x4000 Down   0x8000 Left
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <godot_cpp/classes/display_server.hpp>
#include <godot_cpp/classes/input.hpp>
#include <godot_cpp/variant/array.hpp>

namespace dc_pad {

struct State {
    uint32_t bits = 0;
    float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1, up is -1 (like the game's raw bytes)
};

constexpr float kAxisScale = 1.33f;   // PCSX2.ini [Pad1] AxisScale: full tilt is reached at 75 % of the stick's travel
constexpr float kTriggerOn = 0.30f;

inline State read() {
    using namespace godot;
    State s;
    Input *in = Input::get_singleton();
    if (!in) return s;
    // Godot polls the controller for every window: two game windows would both see the same buttons (one lock-on press would lock both players on). Only the window that has the focus listens.
    DisplayServer *ds = DisplayServer::get_singleton();
    if (ds && !ds->window_is_focused()) return s;
    const Array pads = in->get_connected_joypads();
    if (pads.is_empty()) return s;
    const int d = int(pads[0]);
    auto btn = [&](JoyButton b) { return in->is_joy_button_pressed(d, b); };
    auto axis = [&](JoyAxis a) { return float(in->get_joy_axis(d, a)); };
    auto scaled = [](float v) { return std::clamp(v * kAxisScale, -1.0f, 1.0f); };
    if (axis(JOY_AXIS_TRIGGER_LEFT) > kTriggerOn) s.bits |= 0x0001;
    if (axis(JOY_AXIS_TRIGGER_RIGHT) > kTriggerOn) s.bits |= 0x0002;
    if (btn(JOY_BUTTON_LEFT_SHOULDER)) s.bits |= 0x0004;
    if (btn(JOY_BUTTON_RIGHT_SHOULDER)) s.bits |= 0x0008;
    if (btn(JOY_BUTTON_Y)) s.bits |= 0x0010;
    if (btn(JOY_BUTTON_B)) s.bits |= 0x0020;
    if (btn(JOY_BUTTON_A)) s.bits |= 0x0040;
    if (btn(JOY_BUTTON_X)) s.bits |= 0x0080;
    if (btn(JOY_BUTTON_BACK)) s.bits |= 0x0100;
    if (btn(JOY_BUTTON_LEFT_STICK)) s.bits |= 0x0200;
    if (btn(JOY_BUTTON_RIGHT_STICK)) s.bits |= 0x0400;
    if (btn(JOY_BUTTON_START)) s.bits |= 0x0800;
    if (btn(JOY_BUTTON_DPAD_UP)) s.bits |= 0x1000;
    if (btn(JOY_BUTTON_DPAD_RIGHT)) s.bits |= 0x2000;
    if (btn(JOY_BUTTON_DPAD_DOWN)) s.bits |= 0x4000;
    if (btn(JOY_BUTTON_DPAD_LEFT)) s.bits |= 0x8000;
    s.lx = scaled(axis(JOY_AXIS_LEFT_X));
    s.ly = scaled(axis(JOY_AXIS_LEFT_Y));
    s.rx = scaled(axis(JOY_AXIS_RIGHT_X));
    s.ry = scaled(axis(JOY_AXIS_RIGHT_Y));
    return s;
}


// portkit replay (portkit/PORTKIT.md): when the environment variable PK_RECORD_PAD names a file, every tick of real controller / keyboard input is appended to it as
// `lx ly rx ry buttons menu` (the file is flushed every 120 lines and when the process ends); `--pad-file` of the harness scenes replays it.
inline void record(int lx, int ly, int rx, int ry, uint32_t buttons, uint32_t menu) {
    static FILE *f = nullptr;
    static bool tried = false;
    static int lines = 0;
    if (!tried) {
        tried = true;
        const char *path = std::getenv("PK_RECORD_PAD");
        if (path && *path) {
            f = std::fopen(path, "w");
            if (f) {
                std::fprintf(f, "# portkit pad recording: lx ly rx ry buttons menu, one line per game tick\n");
            }
        }
    }
    if (!f) return;
    std::fprintf(f, "%d %d %d %d %u %u\n", lx, ly, rx, ry, buttons, menu);
    if (++lines % 120 == 0) std::fflush(f);
}

}  // namespace dc_pad
