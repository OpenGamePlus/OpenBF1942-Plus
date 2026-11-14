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

Its strings list the seven categories on F1–F7 (F8 toggles radio icons): radio confirm, request, enemy spotted, game; local confirm, alarms, tactics. Each category's messages follow in reverse key order. The key of each message is read from that order and is unverified against the original. Local alarms list 10 messages; we put the last two (bail out, abandon ship) on F7–F8 inside a vehicle (shortcut). The conquest game page also names the closest control point; we offer only attack and defend.

## Use in the engine

A screen-space overlay drawn after the 3D scene:
- crosshair;
- tickets and flags per team, top centre;
- health bar and weapon, bottom left;
- the vehicle's hit points, bottom right;
- a 6-second feed of kills, captures and destroyed vehicles, top right;
- VICTORY / DEFEAT.

The pixel scale is height / 480, rounded. The weapon line shows rounds / spare magazines and RELOADING. There is no minimap or radio text yet, because those systems do not exist.
