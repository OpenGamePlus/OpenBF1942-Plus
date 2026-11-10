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
| `engine/physics.*` | Jolt world, vehicle parts as forces ([ADR 0004](adr/0004-jolt-physics.md)) | `scene`, `sm`, `terrain`, `con`, `math`, Jolt |
| `engine/mapview.cpp` | executable: window, GL, camera | all of the above, SDL2, epoxy |
| `tools/*.py` | Python reference implementations | `rfa` ← `con` ← `sm`, `tm`, `anim` |

## Rules

- Library modules (everything except `mapview.cpp` and `test_engine.cpp`) do not include SDL or GL headers; only executables do.
- Only `physics.cpp` includes Jolt headers; the rest of the engine sees `physics.h` only.
- No cycles between modules.
- The C++ engine never depends on `tools/`; the tools are a reference, not a build input.
- Every reader treats file data as untrusted: bounds-check before reading, check sizes against the file before allocating, reject non-finite numbers that feed indices, throw `std::runtime_error`.
- No `std::regex` on file data (recursive matcher, stack overflow on large input); recursion driven by data (script `run`, template children) has a depth or call budget.

## Accepted exceptions

- `terrain` scans `Terrain.con` lines directly although `con` now exists (marked `shortcut:` in code); move it onto `con` when terrain needs more settings.
