# Debt, risks, open questions

| Item | Kind | Notes |
|---|---|---|
| RFA `unk` directory field | open question | constant per archive; meaning unknown |
| Patch archive override order | unverified | assumed `_NNN` archives override base files |
| `rfa.py extract` to disk on large archives | untested | full-install check was in-memory only |
| Pure-Python LZO speed | accepted debt | ~2 min for non-sound archives; fine for tools |
| CON level load sequence and object file order | unverified | assumed, see [con.md](../formats/con.md#what-the-engine-loads-by-itself) |
| Loose files vs archives priority | unverified | `con.py` assumes loose files win |
| CON method case-insensitivity | unverified | subsystems verified, methods assumed |
| SM collision tree, footer, v10 byte, `alpha` field | open question | triangles and materials decoded; the tree after them is opaque, see [sm.md](../formats/sm.md#collision-block) |
| C++ reads `Terrain.con` by line scan | shortcut | replace with a C++ CON interpreter when objects are placed |
| C++ VFS mounts archives only, not loose files | shortcut | add when a mod needs it |
| Missing colour tiles drawn grey | unverified | original behaviour unknown; only over open sea so far |
| `texOffsetX/Y`, material map, detail texture, terrain LOD | not implemented | see [terrain.md](../formats/terrain.md) |
| Water drawn on every map | unverified | Berlin has `waterLevel` but no `Water.*` settings |
| Little-endian hosts only | accepted | `memcpy` of file integers in C++ |
| Texture `alternativePath` lookup | unverified | tried as `<alt>/<file name>` before the given path |
| Tree sprite orientation | unverified | `corner.y` sign chosen visually |
| Viewer draws LOD 0 / first `LodObject` child only, distance cull 1.5 km | shortcut | per-mesh `setLodDistance` exists in geometry templates |
| Effects, held weapons not drawn | not implemented | later steps |
| Spawner team in the viewer | shortcut | instance `setTeam`, else first `setObjectTemplate`; no game state |
| Soldier root bone and standing pose | unverified | root replaced by identity, 180° turn; ≈ 20° hip flexion in the stand pose, see [anim.md](../formats/anim.md#soldier-root-unverified) |
| Animation frame rate and state machine | shortcut | 25 fps guessed; first animation of a state, looping; no transitions; weapon states by prefix |
| `animations/animationstates.con` loading | unverified | the viewer runs it after the object folders |
| Object lightmap blend | unverified | 2 × lightmap replaces the sun term on lightmapped meshes |
| Fog start, light scale | shortcut | fog from half the view distance; mesh light ×2; terrain drawn unlit (baked lighting) |
| Skinned normals | shortcut | face normals from screen derivatives; posed normals need a bind pose |
| Soldier parts | shortcut | viewer hides `Parachute` and `1p*` children; the engine switches parts by camera and state |
| C++ vs Python edge differences | accepted | C++ `pose` loops and interpolates (Python: whole frames, clamped); C++ splits CON tokens and lower-cases ASCII only (Python also splits on `\xa0`, `\x85`); `init.con` mod paths are a line scan without `if` |
| `.baf` block second byte | open question | always present, unused by the decoder |
| TM collision block size | open question | same layout as `.sm` blocks, but its tree length is unknown; geometry is found from the end of the file |
| No model viewer in the engine | deliberate | step 3 exports OBJ for Blender; rendering starts with the map viewer |
| Networking protocol | risk | undocumented; roadmap step 8 |
| Vehicle handling units | unverified | spring/damping per wheel mass; drive and drag scales are placeholders; see [vehicles.md](../formats/vehicles.md#engine-model-implemented-unverified) |
| Grip, rollover, idle hold | shortcut | friction 1, grip at 80 % of centre-of-mass height, centre of mass at wheel-centre height, no `c_PGF*` or material friction |
| Tank turn rate | unverified | engine yaw `setMaxSpeed` × 8 °/s |
| Physics scope | not implemented | no wings, floats, gears, track/wheel/turret animation; trees and animated meshes have no collision; other spawned vehicles are static; M3A1 and Sexton hulls touch the ground |
| Jolt fetched at configure | accepted | network on first configure; default AVX2 build ([ADR 0004](../architecture/adr/0004-jolt-physics.md)) |
