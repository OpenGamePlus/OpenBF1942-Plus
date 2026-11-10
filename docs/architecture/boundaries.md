# Boundaries

## Modules

| Module | Role | May use |
|---|---|---|
| `engine/rfa.*` | archives, LZO1X | std only |
| `engine/vfs.*` | mod stack, path lookup | `rfa` |
| `engine/dds.*` | texture decoding | std only |
| `engine/terrain.*` | terrain settings, heightmap | `vfs` (key normalisation) |
| `engine/con.*` | CON interpreter (port of `tools/con.py`) | `vfs` |
| `engine/sm.*` | static meshes, `.rs` shaders | `con` (`lower`) |
| `engine/tm.*` | tree meshes | std only |
| `engine/anim.*` | skeletons, skins, animations, pose, skinning | `math` |
| `engine/math.h` | 4x4 matrices, game rotation convention | std only |
| `engine/scene.*` | instances → mesh paths + world matrices | `con`, `vfs`, `math` |
| `engine/damage.*` | MaterialManager damage table | std only |
| `engine/conquest.*` | flags and tickets | `con` |
| `engine/physics.*` | Jolt world, vehicle parts as forces ([ADR 0004](adr/0004-jolt-physics.md)) | `scene`, `sm`, `terrain`, `con`, `math`, Jolt |
| `engine/nav.*` | path-finding maps, A* | std only |
| `engine/audio.*` | `.wav`, `.ssc`, software mixer ([ADR 0006](adr/0006-audio.md)) | std only |
| `engine/sound.*` | template → sound script, file cache, plays on the mixer | `audio`, `con`, `vfs` |
| `engine/game.*` | game layer: vehicles, soldiers, bots, weapons, damage, conquest, AI, demos, camera | `physics`, `scene`, `damage`, `conquest`, `nav`, `con`, `terrain` |
| `engine/render.*` (library `obf_render`) | OpenGL renderer: terrain, meshes, skinning, sky, tracers | `game` (views), `scene`, `sm`, `tm`, `anim`, `dds`, epoxy |
| `engine/mapview.cpp` | executable: arguments, window, input, audio device, loop | `game`, `render`, `sound`, SDL2 |
| `engine/sim.cpp` | executable `obf_sim`: the game without window or GL | `game` |
| `tools/*.py` | Python reference implementations | `rfa` ← `con` ← `sm`, `tm`, `anim`, `pathfinding` |

## Rules

- No module includes SDL except `mapview.cpp`; only `render.cpp` and `mapview.cpp`'s window code touch GL. The game layer (`obf`) runs headless (`obf_sim`, tests).
- Our sources build with `-ffp-contract=off`: no fused multiply-add, so a simulation gives the same bits in every file and build (needed for replays and networking, [ADR 0005](adr/0005-network-compatibility.md)).
- Only `physics.cpp` includes Jolt headers; the rest of the engine sees `physics.h` only.
- No cycles between modules.
- The C++ engine never depends on `tools/`; the tools are a reference, not a build input.
- Every reader treats file data as untrusted: bounds-check before reading, check sizes against the file before allocating, reject non-finite numbers that feed indices, throw `std::runtime_error`.
- No `std::regex` on file data (recursive matcher, stack overflow on large input); recursion driven by data (script `run`, template children) has a depth or call budget.

## Accepted exceptions

- `terrain` scans `Terrain.con` lines directly although `con` now exists (marked `shortcut:` in code); move it onto `con` when terrain needs more settings.
