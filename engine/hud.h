// HUD: bitmap fonts (.font) and the on-screen layout as textured / solid quads. No GL: render.cpp draws them.
// Spec: docs/formats/hud.md
#pragma once
#include <array>
#include <string>
#include <vector>

namespace obf {

struct Glyph {
    int x0 = 0, y = 0, x1 = -1;  // texels, x1 inclusive; x1 < x0: none
};
struct Font {
    std::string texture;  // VFS path
    int tex_w = 128, tex_h = 128, height = 0, between = 1, space = 1;
    std::array<Glyph, 256> glyphs{};  // by Latin-1 code
};
Font font_parse(const std::string& text);  // throws std::runtime_error without a header or glyphs

struct HudQuad {
    float x, y, w, h;          // pixels, top-left origin
    float u0, v0, u1, v1;      // font texture; u0 < 0: solid colour
    float rgba[4];
};

// What the game shows; built by Game::hud_info.
struct HudInfo {
    bool on = false;  // a player exists
    int team = 0, hp = 0, winner = 0;
    int tickets[2] = {}, flags[2] = {};
    std::string weapon, vehicle;
    int vehicle_hp = -1;            // -1: on foot
    int rounds = -1, spare = 0;     // rounds -1: unlimited (not shown)
    bool reloading = false;
    std::vector<std::string> feed;  // newest last
};

float text_width(const Font& f, const std::string& s, float scale);
void hud_text(const Font& f, const std::string& s, float x, float y, float scale, const float rgba[4], std::vector<HudQuad>& out);
std::vector<HudQuad> hud_layout(const Font& f, const HudInfo& info, int w, int h);

}  // namespace obf
