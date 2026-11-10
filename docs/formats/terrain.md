# Terrain format

Status: **verified** — `obf_mapview` loads all 78 levels of bf1942, XPack1 and XPack2. Height formula and axes fitted against placed objects on all 21 base maps.

## Settings (`<Map>/Init/Terrain.con`)

`GeometryTemplate.create patchTerrain terrainGeometry`, then:

| Key | Use | Values seen |
|---|---|---|
| `file` | heightmap path, no extension (`.raw`) | |
| `worldSize` | map side in metres | 1024, 2048, 4096 |
| `yScale` | height scale | 0.35–1 |
| `waterLevel` | water plane height in metres | |
| `texBaseName` | colour tile prefix (`.../Textures/Tx`) | |
| `materialMap`, `materialSize` | material map, side in samples | not used yet |
| `texOffsetX`, `texOffsetY` | | meaning unknown; ignoring them shows no visible misalignment |
| `detailTexName`, `targetTriCount`, `lodDistance`, `seaFloorLevel`, `waveHeight` | | not used yet |

Values can carry junk (`0.6\``); parse the numeric prefix, as `atof` does.

## Heightmap (`.raw`)

- `n × n` little-endian `u16`, no header; `n` = 256, 512 or 1024. Always 4 m per sample (`worldSize / n`).
- Sample at row `r`, column `c` lies at world `x = c × 4`, `z = r × 4`.
- `height = value × yScale / 256` metres.

Check: 57–90 % of placed objects per map sit within 0.5 m of the nearest sample (the rest are bridges, roofs, elevated props); with x/z swapped the match drops to 0–18 % on all but two flat city maps.

## Colour tiles (`<texBaseName>AAxBB.dds`)

- DXT1, `1024²` with mipmaps on the maps checked.
- `ntiles × ntiles` grid over the whole map; `AA` along x, `BB` along z; texture u along x, v along z. Verified visually: only this mapping is seamless.
- Grids can be sparse (Battle of Britain, Tobruk): tiles over open sea are missing; the grid size is the highest index + 1.

## Material map (`Materialmap.raw`)

`materialSize²` bytes. Not decoded.
