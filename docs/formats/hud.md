# HUD: fonts

Status: **`.font` decoded and drawn: `font/bf1942.font` and its gray TGA, checked in a screenshot. `.dif` not decoded. The original HUD layout and textures are not read: our layout is a stand-in.** Code: `engine/hud.*`, drawn by `Renderer::hud`.

## `.font` (text, CRLF)

`font/bf1942.font` and `font/copy of bf1942.font`.

```
Texture = font/BF1942.tga
TextureWidth = 128
TextureHeight = 128
BetweenWidth = 1
SpaceWidth = 1
Height = 11
AlphaTest = 0
AlphaBlend = 1
-------
<char> x0 y x1        one line per glyph
```

- `<char>` is the first byte of the line, Latin-1; it can be a space.
- Each glyph spans texels `x0..x1` (inclusive) by `Height` rows from `y`, so glyph width is `x1 − x0 + 1`. Neighbouring glyphs start at `x1 + 1`.
- Characters advance by width + `BetweenWidth`.
- `SpaceWidth = 1` is too narrow for a space. We use a third of `Height` (unverified).
- The texture is a TGA of type 3: 8-bit gray, read as alpha.

## `.dif` (not decoded)

The `font/<language>/` folders hold `trebuchet ms8/11/14/18` and `standard6`, each in a plain and a ` - latin` variant, with a TGA per `.dif`. The `.dif` files are text: a `header` line, a version (`2`), the face name, the texture size (256), then `glyphs <count>` and tab-separated rows.

## `lexiconAll.dat` (texts, loose in `Mods/<mod>/`)

uint32 record count, uint32 strings per record (9 in bf1942), then UTF-16LE zero-terminated strings: per record a key and one text per language. The first record is `LANGUAGE` with the language names: English, French, Italian, Spanish, German, Japanese, Korean, Chinese. bf1942 has 1,693 records, 1,655 distinct keys. The radio uses `RADIO_HEADING_<x>` for menu entries and `RADIO_<x>` for sent messages (`RADIO_HEADING_LOCAL_FIRE_IN_HOLE` = "FIRE IN THE HOLE!", `RADIO_LOCAL_FIRE_IN_HOLE` = "Fire in the hole!").

## Radio menu (`menu/RadioMenu`, a MemeFile)

Key layout from the Mac client decompilation (`Radio::handleRadioInput`; F1 as the first key is inferred). Closed, F1–F7 open a category; F8 shows / hides the category list (not drawn here). In a category, F1–F7 send:

| Category | Keys |
|---|---|
| F1 radio confirm | F1 roger, F2 negative |
| F2 request | F1 pickup, F2 reinforcement, F3 anti-tank, F4 naval, F5 air, F6 artillery ready, F7 APC |
| F3 spotted | F1 armor, F2 infantry, F3 unit, F4 ship, F5 submarine, F6 airplane, F7 scout |
| F4 game (conquest) | F1–F3 control points 1–3, F4 the closest, F5–F7 points 4–6: "defend" if the sender's team holds it, else "attack" (CTF: F4–F7 flag messages, not done) |
| F5 local confirm | F3 defending, F4 attacking, F5 roger, F6 negative |
| F6 local alarms | F1 wait, F2 fire, F3 hold fire, F4 fire in the hole, F5 medic (in a vehicle: need repairs), F6 take cover (aircraft: check your six), F7 bail out (ship: abandon ship) |
| F7 local tactics | F1 cover me, F2 hold position, F3 go for enemy flag, F4 fall back, F5 stick together (in a vehicle: get in), F6 follow me, F7 go |

Radio messages reach the whole team; local ones only teammates within 70 m (`GameServer::radioMessage`). The AI only receives (`AIRadio`); bots send nothing.

## Use in the engine

A screen-space overlay drawn after the 3D scene:
- crosshair;
- tickets and flags per team, top centre;
- health bar and weapon, bottom left;
- the vehicle's hit points, bottom right;
- a 6-second feed of kills, captures and destroyed vehicles, top right;
- VICTORY / DEFEAT.

The pixel scale is height / 480, rounded. The weapon line shows rounds / spare magazines and RELOADING. There is no minimap or radio text yet, because those systems do not exist.
