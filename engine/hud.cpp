#include "hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace obf {

Font font_parse(const std::string& text) {
    Font f;
    std::istringstream in(text);
    std::string line;
    bool header = true;
    int glyphs = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            if (line.rfind("---", 0) == 0) {
                header = false;
                continue;
            }
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            k.erase(std::remove(k.begin(), k.end(), ' '), k.end());
            v.erase(0, v.find_first_not_of(' '));
            int n = std::atoi(v.c_str());
            if (k == "Texture") f.texture = v;
            else if (k == "TextureWidth") f.tex_w = n;
            else if (k == "TextureHeight") f.tex_h = n;
            else if (k == "Height") f.height = n;
            else if (k == "BetweenWidth") f.between = n;
            else if (k == "SpaceWidth") f.space = n;
            continue;
        }
        if (line.size() < 2) continue;
        // "<char> x0 y x1": the first byte is the character (a space too)
        std::istringstream ls(line.substr(1));
        Glyph g;
        if (!(ls >> g.x0 >> g.y >> g.x1)) continue;
        if (g.x0 < 0 || g.y < 0 || g.x1 >= f.tex_w || g.y + f.height > f.tex_h) continue;
        f.glyphs[static_cast<unsigned char>(line[0])] = g, ++glyphs;
    }
    if (header || f.height <= 0 || f.tex_w <= 0 || f.tex_h <= 0 || !glyphs) throw std::runtime_error("font: no header or glyphs");
    for (char& c : f.texture) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return f;
}

namespace {
float advance(const Font& f, unsigned char c) {
    const Glyph& g = f.glyphs[c];
    // shortcut: SpaceWidth 1 reads as one texel; spaces get a third of the height until the original is measured
    if (c == ' ') return static_cast<float>(std::max(f.space, f.height / 3) + f.between);
    return g.x1 >= g.x0 ? static_cast<float>(g.x1 - g.x0 + 1 + f.between) : 0.0f;
}
}  // namespace

float text_width(const Font& f, const std::string& s, float scale) {
    float w = 0;
    for (unsigned char c : s) w += advance(f, c);
    return w * scale;
}

void hud_text(const Font& f, const std::string& s, float x, float y, float scale, const float rgba[4], std::vector<HudQuad>& out) {
    for (unsigned char c : s) {
        const Glyph& g = f.glyphs[c];
        if (c != ' ' && g.x1 >= g.x0) {
            float gw = static_cast<float>(g.x1 - g.x0 + 1);
            float tw = static_cast<float>(f.tex_w), th = static_cast<float>(f.tex_h);
            out.push_back({x, y, gw * scale, static_cast<float>(f.height) * scale, static_cast<float>(g.x0) / tw, static_cast<float>(g.y) / th,
                           static_cast<float>(g.x1 + 1) / tw, static_cast<float>(g.y + f.height) / th, {rgba[0], rgba[1], rgba[2], rgba[3]}});
        }
        x += advance(f, c) * scale;
    }
}

std::vector<HudQuad> hud_layout(const Font& f, const HudInfo& in, int w, int h) {
    std::vector<HudQuad> q;
    if (!in.on) return q;
    const float s = std::max(1.0f, std::round(static_cast<float>(h) / 480.0f)), W = static_cast<float>(w), H = static_cast<float>(h);
    const float white[4] = {1, 1, 1, 0.9f}, shade[4] = {0, 0, 0, 0.45f}, ally[4] = {0.55f, 0.75f, 1, 1}, axis[4] = {1, 0.55f, 0.45f, 1};
    auto solid = [&](float x, float y, float bw, float bh, const float c[4]) { q.push_back({x, y, bw, bh, -1, 0, 0, 0, {c[0], c[1], c[2], c[3]}}); };
    auto text = [&](const std::string& t, float x, float y, const float c[4]) { hud_text(f, t, x, y, s, c, q); };
    const float lh = static_cast<float>(f.height + 2) * s;

    // crosshair: four ticks around the centre
    float cx = std::round(W / 2), cy = std::round(H / 2), t = 2 * s, len = 6 * s, gap = 4 * s;
    solid(cx - gap - len, cy - t / 2, len, t, white), solid(cx + gap, cy - t / 2, len, t, white);
    solid(cx - t / 2, cy - gap - len, t, len, white), solid(cx - t / 2, cy + gap, t, len, white);

    // tickets and flags, top centre: team 1 (axis in the retail conquest maps) left, team 2 right
    std::string t1 = std::to_string(in.tickets[0]) + "  [" + std::to_string(in.flags[0]) + "]";
    std::string t2 = "[" + std::to_string(in.flags[1]) + "]  " + std::to_string(in.tickets[1]);
    float half = std::max(text_width(f, t1, s), text_width(f, t2, s)) + 16 * s;
    solid(W / 2 - half, 4 * s, 2 * half, lh + 4 * s, shade);
    text(t1, W / 2 - 10 * s - text_width(f, t1, s), 6 * s, axis), text(t2, W / 2 + 10 * s, 6 * s, ally);

    // health bar and weapon, bottom left; vehicle bottom right
    float by = H - 10 * s - lh;
    solid(10 * s, by, 104 * s, lh, shade);
    solid(12 * s, by + 2 * s, static_cast<float>(std::clamp(in.hp, 0, 100)) * s, lh - 4 * s, in.hp > 25 ? ally : axis);
    text(std::to_string(std::max(0, in.hp)), 120 * s, by + s, white);
    if (!in.weapon.empty()) {
        std::string wl = in.weapon;
        if (in.rounds >= 0) wl += "  " + std::to_string(in.rounds) + " / " + std::to_string(in.spare) + (in.reloading ? "  RELOADING" : "");
        text(wl, 10 * s, by - lh, in.rounds == 0 && in.spare == 0 && !in.reloading ? axis : white);
    }
    if (in.vehicle_hp >= 0) {
        std::string v = in.vehicle + "  " + std::to_string(in.vehicle_hp);
        text(v, W - 10 * s - text_width(f, v, s), by, white);
    }

    // kill / capture feed, top right, newest at the bottom
    float fy = 6 * s;
    for (const std::string& m : in.feed) text(m, W - 10 * s - text_width(f, m, s), fy, white), fy += lh;

    if (in.winner) {
        std::string m = in.winner == in.team ? "VICTORY" : "DEFEAT";
        hud_text(f, m, W / 2 - text_width(f, m, 2 * s) / 2, H / 3, 2 * s, white, q);
    }
    return q;
}

}  // namespace obf
