// HUD: bitmap fonts (.font) and the on-screen layout as textured / solid quads. No GL: render.cpp draws them.
// Spec: docs/formats/hud.md
#pragma once
#include <array>
#include <cstdint>
#include <map>
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
// lexiconAll.dat (Mods/<mod>/, loose): uint32 records, uint32 strings per record, then UTF-16LE zero-terminated
// strings, a key and one text per language (the first record names them: English, French, Italian, Spanish,
// German, Japanese, Korean, Chinese). Returns key -> text in language `lang` (0 English), as Latin-1 ('?' beyond).
std::map<std::string, std::string> lexicon_parse(const std::vector<uint8_t>& data, int lang = 0);

struct HudQuad {
    float x, y, w, h;          // pixels, top-left origin
    float u0, v0, u1, v1;      // texture coordinates; u0 < 0: solid colour
    float rgba[4];
    std::string tex{};         // VFS path without extension; empty: the font; "@minimap": the level's map
    float angle = 0;           // radians, clockwise about the centre
};

struct MapIcon {  // a minimap marker
    float x = 0, z = 0, yaw = 0;  // world position, heading (radians, 0 = +z, clockwise)
    std::string tex;              // VFS path without extension
    float size = 16;              // setMinimapIconSize (texels at 640 x 480)
    float rgba[4] = {1, 1, 1, 1};
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
    // radio bar: closed, the category row (F8 shows / hides it); open, its category on top and a row of messages
    struct RadioButton {
        std::string icon, label;  // menu/texture/radio/icon_<x>; empty icon: nothing on that key
    };
    bool radio_bar = false;
    int radio_open = -1;                  // the open category (0-6), -1 closed
    std::vector<RadioButton> radio_row;   // 8 keys: the categories (closed) or the messages and F8 cancel (open)
    // original HUD textures (VFS paths without extension; empty: drawn as plain bars and text)
    std::string health_empty, health_full;  // menu/texture/ingame/healthbar_*_<kit>_64x64 (the vehicle's on board)
    std::string ammo_empty, ammo_full;      // the weapon's setAmmoBar / setAmmoBarFill
    float ammo_rows = 0;                    // setAmmoBarSize: texels of the fill, from the bottom
    float ammo_x = 5, ammo_y = -11;         // setAmomBarPosX / PosY (sic): from the panel's left, the screen's bottom
    std::string flag_tex[2];                // teams 1, 2: their soldiers' setMinimapIcon (national flag)
    int mag = 0;                            // magSize: a full fill
    float world = 0, map_x = 0, map_z = 0;  // minimap: world size (0: none), the point it centres on
    std::vector<MapIcon> icons;
};

float text_width(const Font& f, const std::string& s, float scale);
void hud_text(const Font& f, const std::string& s, float x, float y, float scale, const float rgba[4], std::vector<HudQuad>& out);
std::vector<HudQuad> hud_layout(const Font& f, const HudInfo& info, int w, int h);

}  // namespace obf
