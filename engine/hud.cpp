#include "hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace obf {

std::map<std::string, std::string> lexicon_parse(const std::vector<uint8_t>& data, int lang) {
    std::map<std::string, std::string> out;
    if (data.size() < 8) return out;
    auto u32 = [&](size_t at) { return uint32_t(data[at]) | uint32_t(data[at + 1]) << 8 | uint32_t(data[at + 2]) << 16 | uint32_t(data[at + 3]) << 24; };
    uint32_t records = u32(0), per = u32(4);
    size_t at = 8;
    auto next = [&](std::string& s) {  // one zero-terminated UTF-16LE string as Latin-1; false at the end of the data
        s.clear();
        for (; at + 1 < data.size(); at += 2) {
            unsigned c = data[at] | unsigned(data[at + 1]) << 8;
            if (!c) return at += 2, true;
            s += c < 256 ? static_cast<char>(c) : '?';
        }
        return false;
    };
    std::vector<std::string> rec(per);
    for (uint32_t r = 0; r < records && per > 1; ++r) {
        for (auto& s : rec)
            if (!next(s)) return out;
        if (r > 0 && static_cast<size_t>(lang) + 1 < per) out[rec[0]] = rec[static_cast<size_t>(lang) + 1];
    }
    return out;
}

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
    auto tex = [&](const std::string& t, float x, float y, float tw, float th, float u0, float v0, float u1, float v1) {
        q.push_back({x, y, tw, th, u0, v0, u1, v1, {1, 1, 1, 1}, t, 0});
    };
    // shortcut: placement and scale are ours (the original's are in menu/InGame, a MemeFile not decoded)
    if (!in.health_empty.empty()) {  // 64 x 64 texels; the bar is columns 0-21, rows 2-61, filled from the bottom
        const float px = 10 * s, py = H - 10 * s - 64 * s, top = 2 + (1 - static_cast<float>(std::clamp(in.hp, 0, 100)) / 100) * 59;
        tex(in.health_empty, px, py, 64 * s, 64 * s, 0, 0, 1, 1);
        tex(in.health_full, px, py + top * s, 22 * s, (64 - top) * s, 0, top / 64, 22.0f / 64, 1);
        text(std::to_string(std::max(0, in.hp)), px + 44 * s, py + 30 * s, white);
        by = py - 2 * s;
    } else {
        solid(10 * s, by, 104 * s, lh, shade);
        solid(12 * s, by + 2 * s, static_cast<float>(std::clamp(in.hp, 0, 100)) * s, lh - 4 * s, in.hp > 25 ? ally : axis);
        text(std::to_string(std::max(0, in.hp)), 120 * s, by + s, white);
    }
    if (!in.ammo_empty.empty() && in.rounds >= 0) {  // the magazine, 32 x 64 texels, filled from the bottom; numbers beside
        const float px = W - 10 * s - 64 * s, py = H - 10 * s - 64 * s;
        float rows = std::clamp(in.ammo_rows, 0.0f, 64.0f) * (in.mag > 0 ? std::clamp(static_cast<float>(in.rounds) / static_cast<float>(in.mag), 0.0f, 1.0f) : 1.0f);
        tex(in.ammo_empty, px, py, 32 * s, 64 * s, 0, 0, 1, 1);
        if (!in.reloading) tex(in.ammo_full, px, py + (64 - rows) * s, 32 * s, rows * s, 0, (64 - rows) / 64, 1, 1);
        std::string n = in.reloading ? "RELOADING" : std::to_string(in.rounds) + " / " + std::to_string(in.spare);
        text(n, px + 34 * s, py + 46 * s, in.rounds == 0 && in.spare == 0 && !in.reloading ? axis : white);
        text(in.weapon, W - 10 * s - text_width(f, in.weapon, s), py - lh, white);
    } else if (!in.weapon.empty()) {
        std::string wl = in.weapon;
        if (in.rounds >= 0) wl += "  " + std::to_string(in.rounds) + " / " + std::to_string(in.spare) + (in.reloading ? "  RELOADING" : "");
        text(wl, 10 * s, by - lh, in.rounds == 0 && in.spare == 0 && !in.reloading ? axis : white);
    }
    if (in.vehicle_hp >= 0) {
        std::string v = in.vehicle + "  " + std::to_string(in.vehicle_hp);
        text(v, W - 10 * s - text_width(f, v, s), in.ammo_empty.empty() ? by : H - 10 * s - 64 * s - 2 * lh, white);
    }

    // minimap, top left: the level's colour map 2 x 250 m around the player, north up (shortcut: the original's
    // zoom levels, frame and placement are not read), with its markers
    if (in.world > 0) {
        const float ms = 120 * s, mx = 10 * s, my = 10 * s, r = 250;
        solid(mx - s, my - s, ms + 2 * s, ms + 2 * s, shade);
        q.push_back({mx, my, ms, ms, (in.map_x - r) / in.world, (in.map_z + r) / in.world, (in.map_x + r) / in.world, (in.map_z - r) / in.world,
                     {1, 1, 1, 1}, "@minimap", 0});
        for (const MapIcon& ic : in.icons) {
            float ix = mx + (ic.x - in.map_x + r) / (2 * r) * ms, iy = my + (in.map_z + r - ic.z) / (2 * r) * ms, sz = ic.size * 0.5f * s;
            if (ix < mx || iy < my || ix > mx + ms || iy > my + ms) continue;
            q.push_back({ix - sz / 2, iy - sz / 2, sz, sz, 0, 0, 1, 1, {ic.rgba[0], ic.rgba[1], ic.rgba[2], ic.rgba[3]}, ic.tex, ic.yaw});
        }
    }

    // kill / capture feed, top right, newest at the bottom
    float fy = 6 * s;
    for (const std::string& m : in.feed) text(m, W - 10 * s - text_width(f, m, s), fy, white), fy += lh;

    // open radio menu, left of centre: its heading, then one message per key (shortcut: plain text, the original's
    // icons and colours in menu/RadioMenu are not drawn)
    if (!in.radio.empty()) {
        float ry = H / 3, rw = 0;
        for (const std::string& m : in.radio) rw = std::max(rw, text_width(f, m, s));
        solid(8 * s, ry - 4 * s, rw + 12 * s, lh * static_cast<float>(in.radio.size()) + 6 * s, shade);
        for (size_t i = 0; i < in.radio.size(); ++i) text(in.radio[i], 14 * s, ry, i ? white : (in.team == 1 ? axis : ally)), ry += lh;
    }

    if (in.winner) {
        std::string m = in.winner == in.team ? "VICTORY" : "DEFEAT";
        hud_text(f, m, W / 2 - text_width(f, m, 2 * s) / 2, H / 3, 2 * s, white, q);
    }
    return q;
}

}  // namespace obf
