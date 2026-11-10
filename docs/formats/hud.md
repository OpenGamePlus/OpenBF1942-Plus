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

## Use in the engine

A screen-space overlay drawn after the 3D scene:
- crosshair;
- tickets and flags per team, top centre;
- health bar and weapon, bottom left;
- the vehicle's hit points, bottom right;
- a 6-second feed of kills, captures and destroyed vehicles, top right;
- VICTORY / DEFEAT.

The pixel scale is height / 480, rounded. There is no ammo, minimap or radio text yet, because those systems do not exist.
