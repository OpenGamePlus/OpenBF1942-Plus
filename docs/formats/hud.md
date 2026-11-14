# HUD: fonts

Status: **`.font` decoded and drawn: `font/bf1942.font` and its gray TGA, checked in a screenshot. Minimap from the colour tiles and `setMinimapIcon`; health bar and magazine textures. `.dif` not decoded. The original HUD layout and textures are not read: our layout is a stand-in.** Code: `engine/hud.*`, drawn by `Renderer::hud`.

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

Key layout from the Mac client decompilation (`Radio::handleRadioInput`; F1 as the first key is inferred). Closed, F1–F7 open a category; F8 shows / hides the category list. In a category, F1–F7 send:

| Category | Keys |
|---|---|
| F1 radio confirm | F1 roger, F2 negative |
| F2 request | F1 pickup, F2 reinforcement, F3 anti-tank, F4 naval, F5 air, F6 artillery ready, F7 APC |
| F3 spotted | F1 armor, F2 infantry, F3 unit, F4 ship, F5 submarine, F6 airplane, F7 scout |
| F4 game (conquest) | F1–F3 control points 1–3, F4 the closest, F5–F7 points 4–6: "defend" if the sender's team holds it, else "attack" (CTF: F4–F7 flag messages, not done) |
| F5 local confirm | F3 defending, F4 attacking, F5 roger, F6 negative |
| F6 local alarms | F1 wait, F2 fire, F3 hold fire, F4 fire in the hole, F5 medic (in a vehicle: need repairs), F6 take cover (aircraft: check your six), F7 bail out (ship: abandon ship) |
| F7 local tactics | F1 cover me, F2 hold position, F3 go for enemy flag, F4 fall back, F5 stick together (in a vehicle: get in), F6 follow me, F7 go |

The maintainer's captures of the original client show the same layout (each category opened once) and how it is drawn: a row of buttons at the top left, each a 64 × 32 texture from `menu/texture/radio/` holding the frame and icon (`icon_f1`–`icon_f8` for the categories, `icon_roger`, `icon_pickup`, `icon_controlpoint_1`–`7` and `_closest`, `icon_cancel`…), with the key ("F1") written on it and a label below (`RADIO_HEADING_<x>`; `RADIO_HEADING_ON_OFF` for F8). Closed: the eight categories in slots at x 31, 96, 160, 225, then 331, 396, 461, 526 (800 × 600 units), labels at y 30. Open: the category's button above its slot, its messages and F8 CANCEL in the row below (y 29), labels on two alternating lines (y 56, 67). Some buttons are drawn greyed; the rule is not known (we draw all lit). F8 shows or hides the closed row.

Radio messages reach the whole team; local ones only teammates within 70 m (`GameServer::radioMessage`). The AI only receives (`AIRadio`); bots send nothing.

## Use in the engine

A screen-space overlay drawn after the 3D scene:
- crosshair;
- tickets per team with their flags, above the minimap (top right);
- health bar and weapon, bottom left;
- the vehicle's hit points, bottom right;
- a 6-second feed of kills, captures and destroyed vehicles, top left;
- VICTORY / DEFEAT.

The pixel scale is height / 480, rounded. The weapon line shows rounds / spare magazines and RELOADING.

## HUD textures (`menu/texture/ingame/`)

- `healthbar_empty_<kit>_64x64` / `healthbar_full_<kit>_64x64` (`assault`, `at`, `engineer`, `medic`, `scout`): a segmented bar in columns 0–21, rows 2–61 (12 segments), and the kit's icon bottom right. We draw the empty one, then the full one's bar from the bottom up to the hit points. The kit is told by its template name (`GB_Assault`).
- Weapons name their magazine: `setAmmoBar` / `setAmmoBarFill "Ingame/Magbar_Pistol_full_32x64.tga"` and `setAmmoBarSize` (41 for the pistol: the magazine's height in texels from the bottom, matching the art). We fill `setAmmoBarSize × rounds / magSize` rows. `setAmomBarPosX/Y` and `setAmomBarTextPosX/Y` (sic; some files spell `Ammo`) place it in the panel; not read.
- Their placement is in `menu/InGame` (a MemeFile: `Soldier/SoldierHealthBarIcon`, `Ammo/SoldierAmmo/SoldierAmmoBar…`), not decoded. Measured instead on maintainer captures of the original client at 1920 × 1080 (El Alamein): the HUD is laid out for **800 × 600 and stretched on each axis** (× 2.4, × 1.8 there). In those units: health texture at (47.5, 525), 64 × 64; ammunition panel (`ammobar_soldier_panel_64x64`) at (691, 527), 64 × 64, rounds at its top right in a large font, spare magazines in its tab; the magazine at x = panel + `setAmomBarPosX`, its bottom at 600 + `setAmomBarPosY` (the BAR's 5 and −11 match the capture within 2 units); minimap 173 × 171 at (621, 32); above it three boxes from y 5.5 to 26: allies' flag and tickets (blue) at 621, an empty box at 691, axis (red) at 727; kill messages top left, under a row of radio buttons (F1–F8 with icons, see the radio section).

## Vehicle HUD

Each seat's `PlayerControlObject` names it: `setVehicleIcon "Vehicle/Icon_sherman.tga"` (a 128 × 128 picture, `menu/texture/vehicle/`), `setVehicleIconPos 54/103` (the seat's spot on it, texels; the gunner's seat of the same tank says 32/61), `setNumberOfWeaponIcons`, `setPrimaryAmmoIcon` / `setSecondaryAmmoIcon` (`menu/texture/ammo/icon_cannon`…, 64 × 64, art in the lower half) and `setPrimaryAmmoBar` / `setSecondaryAmmoBar` (`ABAmmoBarReloadBar`, `ABAmmoBarHeatBar`).

Measured on the captures (800 × 600 units): the soldier's health bar stays; the vehicle's green bar (`vehicle_healthbar_*_32x64`) at (174, 525.5); the picture at (199, 450), 128 × 128, with a blue dot at the icon position (matches within a unit); per weapon, primary from x 550 and secondary from 670: a small bar (reload: green and segmented; heat: dark) at y 547, the ammunition bar (`ammobar_*_32x64`) 19 to the right, then the panel (`ammobar_vehicle_panel_64x64`) with the icon 40 to the right, the count at its top left (we show the seat's rounds on the primary and fill its bar by rounds / `magSize`; the secondary is not fired, so its bar stays full without a count). A single weapon icon goes in the right slot (guessed). Also seen: a soldier posture figure beside the health bar and, for a tank's turret, a direction dial at the bottom centre (`icon_tank_turn_*`); neither drawn.

## Minimap

Templates name their marker with `setMinimapIcon "Minimap/minimap_icon_tank_16x16.tga"` (read as `menu/texture/minimap/<name>`, `.dds` in `menu.rfa`) and its size with `setMinimapIconSize` (16 when absent; 32 and 64 for ships). Soldier templates set their national flag (`flag_us.tga`, `menu/texture/flag_us`). `menu/texture/minimap/` also holds `map_circle`, `map_dot`, class icons and `Objectives/Objective000`–`100` (capture progress, not drawn).

No level ships a map picture: the level's colour tiles are the top view. We shrink them into a 512² texture (nearest texel, as the terrain shader samples them: tile `Tx<a>x<b>` covers x from a, z from b) and tint what lies under the water level.

Drawn top right as measured above, north up, 2 × 200 m around the player (estimated from the grid lines in a capture; the original's zoom levels are not read), with the level's grid (8 × 8 cells; the capture labels the player's cell, `G6`: not drawn). Markers: control points (the owner's flag, else a grey circle), vehicles with an icon turned to their heading (crew colour, empty ones grey), teammates on foot (dots), the player (yellow). Enemies are not shown.
