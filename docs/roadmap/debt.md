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
| SM collision blocks, footer, v10 byte, `alpha` field | open question | parsed as opaque, see [sm.md](../formats/sm.md) |
| C++ reads `Terrain.con` by line scan | shortcut | replace with a C++ CON interpreter when objects are placed |
| C++ VFS mounts archives only, not loose files | shortcut | add when a mod needs it |
| Missing colour tiles drawn grey | unverified | original behaviour unknown; only over open sea so far |
| `texOffsetX/Y`, material map, detail texture, terrain LOD | not implemented | see [terrain.md](../formats/terrain.md) |
| Water drawn on every map | unverified | Berlin has `waterLevel` but no `Water.*` settings |
| Little-endian hosts only | accepted | `memcpy` of file integers in C++ |
| Texture `alternativePath` lookup | unverified | tried as `<alt>/<file name>` before the given path |
| Tree sprite orientation | unverified | `corner.y` sign chosen visually |
| Viewer draws LOD 0 only, distance cull 1.5 km | shortcut | per-mesh `setLodDistance` exists in geometry templates |
| `AnimatedMesh`, effects not drawn | not implemented | step 5 and later |
| Spawner team in the viewer | shortcut | instance `setTeam`, else first `setObjectTemplate`; no game state |
| TM collision block layout | open question | skipped by anchoring geometry at the end of file |
| No model viewer in the engine | deliberate | step 3 exports OBJ for Blender; rendering starts with the map viewer |
| Networking protocol | risk | undocumented; roadmap step 8 |
