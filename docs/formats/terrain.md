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
| `materialMap`, `materialSize` | material map, side in samples | read by the engine for impact effects (the ground material a shot hits) |
| `texOffsetX`, `texOffsetY` | | meaning unknown; ignoring them shows no visible misalignment |
| `detailTexName` | detail texture, one per level (Berlin: grey gravel, 512 × 512 DXT5, mean ≈ 0.55 on every level checked) | multiplied into the colour tiles, normalised to its mean, one repeat per 4 m, faded out by 200 m. The scale was matched by eye to the original and the blend is guessed |
| `targetTriCount`, `lodDistance`, `seaFloorLevel`, `waveHeight` | | not used yet |

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

## Sky, sun, fog, light (`Init.con`, `Init/SkyAndSun.con`)

| Setting | Use in the viewer | Verified |
|---|---|---|
| `GeometryTemplate.create StandardMesh SkyBox` + `file` | sky box `.sm` (±2000 m, 6 faces), centred on the camera, no depth write | visually |
| `Sky.setRotAngle` | sky box yaw in degrees | no |
| `sky.sunLightDirectionVec` | direction towards the sun for mesh lighting | no |
| `renderer.globalAmbientColor` + `renderer.ambientColor`, `renderer.diffuseColor` | mesh light = 2 × (ambient + diffuse · N·L), capped | no (×2 assumed) |
| `renderer.fogColorVec`, `Game.setViewDistance` | linear fog from half the view distance to the view distance | end only |

Values seen: view distance 500 m (Wake, El Alamein), 200 m (Bulge), 100 m (Berlin). Lens flares, clouds (`Sky.addCloud`) and `renderer.specularColor` are not used yet.

## Object lightmaps (`<Map>/ObjectLightmaps/`)

- One TGA per placed static mesh: `<geometry file>_<x>-<y>-<z>.tga`, position of the placed instance **truncated** to integers (all 67 files on Wake match; none needs rounding). Children of a bundle use the root instance's position. `_m1`/`_m2` variants belong to the detail levels.
- TGA type 1: 8-bit indices with an embedded 24-bit palette, 128² or 256².
- Sampled with the mesh's second UV set (`.sm` stride 40). Lightmaps average 0.13–0.21 in brightness (Wake, El Alamein, Berlin, Bocage), so the viewer multiplies the texture by 2 × lightmap (Direct3D `MODULATE2X`, the usual lightmap blend of the era) instead of the sun term. Unverified.
- `palette.pal` next to them (RIFF `PAL `) is not needed for decoding; its role is unknown.

## Material map (`Materialmap.raw`)

`materialSize²` bytes, one `MaterialManager` id per cell (4 m on the maps checked), same row/column orientation as the heightmap: row = z, column = x (verified: on all 21 base maps none of the 791 spawn points and control points lies on material 7, while with the axes swapped up to all of them do). Every value on Wake, El Alamein and Berlin is a defined material: Wake is mostly 11 "Wet sand", El Alamein 10 "Dry sand" and 12 "Rock", Berlin 7 "Reserved (Outside map)".

**Material 7 is outside the world.** The original counts a point as outside the world when it is past the combat area (`game.setActiveCombatArea x z width height`, else the whole terrain) *or* its material-map cell is 7, even inside that rectangle; being outside costs `damageForBeingOutSideWorld` per second (read from BF Vietnam's executable by RefractorForge, [references.md](../references.md); the predicate's use of material 7 fits our data above, the damage is unverified). Material 7 covers up to 99 % of a map (Berlin; Stalingrad 93 %, Tobruk 92 %): the void around the playable area. No BF1942 template sets `damageForBeingOutSideWorld` or a time allowed outside, so the engine defaults apply; their values are open.

Materials are defined in `bf1942/game/materialManagerdefine.con` (`MaterialManager.material <id>` then `materialFriction`, `materialElasticity`, `materialResistance`, `materialDamage`, `materialAttGroup`, `materialDefGroup`; 155 ids, 0–257); `materialManagerSettings.con` holds the attack × defence `damageMod` matrix. Collision faces use the same ids.
