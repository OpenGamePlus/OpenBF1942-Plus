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

    // The original's layout is made for 800 x 600 and stretched to the screen on each axis: these spots and sizes are
    // measured on maintainer captures at 1920 x 1080 (x 2.4, y 1.8), El Alamein; menu/InGame (a MemeFile) holds them.
    const float sx = W / 800, sy = H / 600;
    auto V = [&](float x, float y, float bw, float bh, const std::string& t, float u0, float v0, float u1, float v1, const float* c = nullptr) {
        const float one[4] = {1, 1, 1, 1};
        if (!c) c = one;
        q.push_back({x * sx, y * sy, bw * sx, bh * sy, u0, v0, u1, v1, {c[0], c[1], c[2], c[3]}, t, 0});
    };
    auto vsolid = [&](float x, float y, float bw, float bh, const float c[4]) { solid(x * sx, y * sy, bw * sx, bh * sy, c); };
    auto vtext = [&](const std::string& t, float x, float y, float height, const float c[4], bool right = false) {
        float sc = std::max(1.0f, std::round(height * sy / static_cast<float>(std::max(1, f.height))));  // a pixel font: whole scales
        hud_text(f, t, x * sx - (right ? text_width(f, t, sc) : 0.0f), y * sy, sc, c, q);
    };
    const float frame[4] = {0.08f, 0.08f, 0.08f, 0.8f}, panel[4] = {0.18f, 0.18f, 0.18f, 0.7f};

    // tickets, above the minimap: allies left (blue), an empty box, axis right (red), each with its flag
    vsolid(621, 5.5f, 66, 20.5f, panel), vsolid(691, 5.5f, 34, 20.5f, panel), vsolid(727, 5.5f, 67, 20.5f, panel);
    for (int k = 0; k < 2; ++k) {  // k 0: team 2 (allies) on the left
        int team = k == 0 ? 2 : 1;
        float bx = k == 0 ? 621 : 727;
        if (!in.flag_tex[team - 1].empty()) V(bx + 4, 9, 18, 13, in.flag_tex[team - 1], 0, 0, 1, 1);
        vtext(std::to_string(in.tickets[team - 1]), bx + 62, 8.5f, 13, team == 2 ? ally : axis, true);
    }

    // health bar (64 x 64 texels at 47.5, 525; the bar is columns 0-21, rows 2-61, filled from the bottom)
    float by = H - 10 * s - lh;
    if (!in.health_empty.empty()) {
        const float top = 2 + (1 - static_cast<float>(std::clamp(in.hp, 0, 100)) / 100) * 59;
        V(47.5f, 525, 64, 64, in.health_empty, 0, 0, 1, 1);
        V(47.5f, 525 + top, 22, 64 - top, in.health_full, 0, top / 64, 22.0f / 64, 1);
        by = 525 * sy - 2 * s;
    } else {
        solid(10 * s, by, 104 * s, lh, shade);
        solid(12 * s, by + 2 * s, static_cast<float>(std::clamp(in.hp, 0, 100)) * s, lh - 4 * s, in.hp > 25 ? ally : axis);
        text(std::to_string(std::max(0, in.hp)), 120 * s, by + s, white);
    }
    // ammunition: the soldier panel (64 x 64 at 691, 527), the weapon's magazine at setAmomBarPosX from the panel and
    // its bottom setAmomBarPosY above the screen's, filled from the bottom; rounds at the top right, magazines in the tab
    if (!in.ammo_empty.empty() && in.rounds >= 0) {
        const float px = 691, py = 527, mx = px + in.ammo_x, my = 600 + in.ammo_y - 64;
        float rows = std::clamp(in.ammo_rows, 0.0f, 64.0f) * (in.mag > 0 ? std::clamp(static_cast<float>(in.rounds) / static_cast<float>(in.mag), 0.0f, 1.0f) : 1.0f);
        V(px, py, 64, 64, "menu/texture/ingame/ammobar_soldier_panel_64x64", 0, 0, 1, 1);
        V(mx, my, 32, 64, in.ammo_empty, 0, 0, 1, 1);
        if (!in.reloading) V(mx, my + 64 - rows, 32, rows, in.ammo_full, 0, (64 - rows) / 64, 1, 1);
        vtext(std::to_string(in.rounds), px + 38, 530, 12, in.rounds == 0 && in.spare == 0 && !in.reloading ? axis : white, true);
        vtext(std::to_string(in.spare), px + 56, 575, 10, white, true);
    } else if (!in.weapon.empty() && in.vehicle_icon.empty()) {
        std::string wl = in.weapon;
        if (in.rounds >= 0) wl += "  " + std::to_string(in.rounds) + " / " + std::to_string(in.spare) + (in.reloading ? "  RELOADING" : "");
        text(wl, 10 * s, by - lh, in.rounds == 0 && in.spare == 0 && !in.reloading ? axis : white);
    }
    if (!in.vehicle_icon.empty()) {
        // vehicle: its green health bar (32 x 64 at 174, 525.5), its picture (128 x 128 at 199, 450) with the seat's
        // dot at setVehicleIconPos, and per weapon (primary at 550, secondary at 670; one icon: the right slot) a
        // small reload / heat bar, the ammunition bar filled by rounds / magSize and a panel with the weapon's icon
        // and its rounds at the top left
        const float rows = 64 * in.vehicle_frac;
        V(174, 525.5f, 32, 64, "menu/texture/ingame/vehicle_healthbar_empty_32x64", 0, 0, 1, 1);
        V(174, 525.5f + 64 - rows, 32, rows, "menu/texture/ingame/vehicle_healthbar_full_32x64", 0, (64 - rows) / 64, 1, 1);
        V(199, 450, 128, 128, in.vehicle_icon, 0, 0, 1, 1);
        const float dot[4] = {0.2f, 0.45f, 1, 1};
        if (in.seat_x >= 0) V(199 + in.seat_x - 3, 450 + in.seat_y - 3, 6, 6, "menu/texture/minimap/map_dot", 0, 0, 1, 1, dot);
        size_t n = in.vehicle_weapons.size();
        for (size_t i = 0; i < n; ++i) {
            const HudInfo::VehicleWeapon& vw = in.vehicle_weapons[i];
            float x = (n == 1 || i == 1) ? 670 : 550;
            // reload bar: green and segmented in the capture (the vehicle health bar's art, shrunk); heat: the empty heat bar
            if (vw.bar == 1) V(x, 547, 24.4f, 41, "menu/texture/ingame/vehicle_healthbar_full_32x64", 0, 0, 1, 1);
            else if (vw.bar == 2) V(x, 545, 32, 64, "menu/texture/ingame/heatbar_empty_32x64", 0, 0, 1, 1);
            const float fill = 64 * std::clamp(vw.frac, 0.0f, 1.0f);
            V(x + 19, 525.5f, 32, 64, "menu/texture/ingame/ammobar_empty_32x64", 0, 0, 1, 1);
            V(x + 19, 525.5f + 64 - fill, 32, fill, "menu/texture/ingame/ammobar_full_32x64", 0, (64 - fill) / 64, 1, 1);
            V(x + 40, 525.5f, 64, 64, "menu/texture/ingame/ammobar_vehicle_panel_64x64", 0, 0, 1, 1);
            V(x + 40, 525.5f, 64, 64, vw.icon, 0, 0, 1, 1);
            if (vw.rounds >= 0) vtext(std::to_string(vw.rounds), x + 44, 528.5f, 12, vw.rounds == 0 ? axis : white);
        }
    } else if (in.vehicle_hp >= 0) {
        std::string v = in.vehicle + "  " + std::to_string(in.vehicle_hp);
        text(v, W - 10 * s - text_width(f, v, s), in.ammo_empty.empty() ? by : 527 * sy - lh, white);
    }

    // minimap, top right (173 x 171 at 621, 32), north up, 2 x 200 m around the player (shortcut: span estimated from
    // the grid lines in a capture; zoom levels not read), the level's 8 x 8 grid, markers
    if (in.world > 0) {
        const float mx = 621, my = 32, mw = 173, mh = 171, r = 200;
        vsolid(mx - 1.5f, my - 1.5f, mw + 3, mh + 3, frame);
        V(mx, my, mw, mh, "@minimap", (in.map_x - r) / in.world, (in.map_z + r) / in.world, (in.map_x + r) / in.world, (in.map_z - r) / in.world);
        const float grid[4] = {1, 1, 1, 0.35f}, cell = in.world / 8;
        for (int k = 1; k < 8; ++k) {
            float gx = (static_cast<float>(k) * cell - in.map_x + r) / (2 * r), gz = (in.map_z + r - static_cast<float>(k) * cell) / (2 * r);
            if (gx > 0 && gx < 1) vsolid(mx + gx * mw, my, 0.5f, mh, grid);
            if (gz > 0 && gz < 1) vsolid(mx, my + gz * mh, mw, 0.6f, grid);
        }
        for (const MapIcon& ic : in.icons) {
            float ix = mx + (ic.x - in.map_x + r) / (2 * r) * mw, iy = my + (in.map_z + r - ic.z) / (2 * r) * mh, sz = ic.size * 0.5f;
            if (ix < mx || iy < my || ix > mx + mw || iy > my + mh) continue;
            q.push_back({(ix - sz / 2) * sx, (iy - sz / 2) * sy, sz * sx, sz * sy, 0, 0, 1, 1, {ic.rgba[0], ic.rgba[1], ic.rgba[2], ic.rgba[3]}, ic.tex, ic.yaw});
        }
    }

    // kill / capture feed, top left below where the original's radio bar sits, newest at the bottom
    float fy = 40 * sy;
    for (const std::string& m : in.feed) text(m, 6 * sx, fy, white), fy += lh;

    // radio bar, top left: 64 x 32-texel buttons (frame and icon) with the key written on them, labels below; key
    // slots measured on the captures. Closed: the categories. Open: its category above its slot, its messages below,
    // labels on two alternating lines (shortcut: all drawn lit; the original greys some, rule unread).
    if (in.radio_bar || in.radio_open >= 0) {
        static const float slot[8] = {31, 96, 160, 225, 331, 396, 461, 526};
        const float bh = 27, label_h = 6.5f, key_h = 8;
        auto button = [&](int k, float y, const HudInfo::RadioButton& b, float label_y, bool key) {
            if (b.icon.empty()) return;
            V(slot[k], y, 64, bh, b.icon, 0, 0, 1, 1);
            if (key) vtext("F" + std::to_string(k + 1), slot[k] + 5, y + 4.5f, key_h, white);
            float sc = std::max(1.0f, std::round(label_h * sy / static_cast<float>(std::max(1, f.height))));
            if (!b.label.empty()) hud_text(f, b.label, (slot[k] + 31) * sx - text_width(f, b.label, sc) / 2, label_y * sy, sc, white, q);
        };
        if (in.radio_open < 0) {
            for (size_t k = 0; k < in.radio_row.size() && k < 8; ++k) button(static_cast<int>(k), 4.4f, in.radio_row[k], 30, true);
        } else {
            int c = std::clamp(in.radio_open, 0, 7);
            button(c, 3.3f, {"menu/texture/radio/icon_f" + std::to_string(c + 1), ""}, 0, false);
            for (size_t k = 0; k < in.radio_row.size() && k < 8; ++k)
                button(static_cast<int>(k), 29, in.radio_row[k], k % 2 ? 67 : 56, true);
        }
    }

    if (in.winner) {
        std::string m = in.winner == in.team ? "VICTORY" : "DEFEAT";
        hud_text(f, m, W / 2 - text_width(f, m, 2 * s) / 2, H / 3, 2 * s, white, q);
    }
    return q;
}

}  // namespace obf
