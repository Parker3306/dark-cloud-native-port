#include "dccore/recolor.h"
#include <algorithm>
#include <cmath>

namespace dccore {

float player_hue(int index) {
    static const float kHue[8] = {355.0f, 215.0f, 125.0f, 52.0f, 285.0f, 28.0f, 180.0f, 320.0f};
    return kHue[((index % 8) + 8) % 8];
}

bool is_face_texture(const std::string& name, const Image& im) {
    if (im.width <= 64 || im.height <= 64) return true;
    size_t end = name.size();
    if (name.size() > 3 && name.compare(name.size() - 3, 3, "dan") == 0) end -= 3;   // the angry face
    return end >= 2 && name[end - 2] == '0' && name[end - 1] == '1';
}

namespace {

void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v) {
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn;
    v = mx;
    s = mx <= 0.0f ? 0.0f : d / mx;
    if (d <= 1e-6f) { h = 0.0f; return; }
    if (mx == r) h = std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h *= 60.0f;
    if (h < 0.0f) h += 360.0f;
}

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b) {
    h = std::fmod(std::fmod(h, 360.0f) + 360.0f, 360.0f) / 60.0f;
    const float c = v * s, x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f)), m = v - c;
    float rr = 0, gg = 0, bb = 0;
    switch (int(h)) {
        case 0: rr = c; gg = x; break;
        case 1: rr = x; gg = c; break;
        case 2: gg = c; bb = x; break;
        case 3: gg = x; bb = c; break;
        case 4: rr = x; bb = c; break;
        default: rr = c; bb = x; break;
    }
    r = rr + m; g = gg + m; b = bb + m;
}

}  // namespace

void recolor_clothes(Image& im, float hue_deg) {
    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
        if (im.rgba[i + 3] < 128) continue;
        float h, s, v;
        rgb_to_hsv(float(im.rgba[i]) / 255.0f, float(im.rgba[i + 1]) / 255.0f, float(im.rgba[i + 2]) / 255.0f, h, s, v);
        const bool skin_band = h < 48.0f || h > 345.0f;   // skin, hair and leather live around orange: only very saturated pixels there are clothes
        const float need = skin_band ? 0.66f : 0.38f;
        if (s < need || v < 0.20f) continue;
        float r, g, b;
        hsv_to_rgb(hue_deg, s, v, r, g, b);
        im.rgba[i] = uint8_t(std::clamp(int(r * 255.0f + 0.5f), 0, 255));
        im.rgba[i + 1] = uint8_t(std::clamp(int(g * 255.0f + 0.5f), 0, 255));
        im.rgba[i + 2] = uint8_t(std::clamp(int(b * 255.0f + 0.5f), 0, 255));
    }
}

bool is_wash_texture(const std::string& name) { return name == "c04b04"; }

void wash_cloth(Image& im, float hue_deg) {
    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
        if (im.rgba[i + 3] < 128) continue;
        float h, s, v;
        rgb_to_hsv(float(im.rgba[i]) / 255.0f, float(im.rgba[i + 1]) / 255.0f, float(im.rgba[i + 2]) / 255.0f, h, s, v);
        if (s >= 0.38f || v < 0.45f) continue;
        float r, g, b;
        hsv_to_rgb(hue_deg, 0.55f, v * 0.96f, r, g, b);
        im.rgba[i] = uint8_t(std::clamp(int(r * 255.0f + 0.5f), 0, 255));
        im.rgba[i + 1] = uint8_t(std::clamp(int(g * 255.0f + 0.5f), 0, 255));
        im.rgba[i + 2] = uint8_t(std::clamp(int(b * 255.0f + 0.5f), 0, 255));
    }
}

}  // namespace dccore
