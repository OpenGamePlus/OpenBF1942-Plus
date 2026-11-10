# Format coverage

Every file type in the retail archives (bf1942 + XPack1 + XPack2) and how far it is understood. Counts and sizes are totals across the three mods.

| Type | Files | Size | State | Notes |
|---|---|---|---|---|
| `.rfa` | archives | 2.2 GB | done | [rfa.md](rfa.md) |
| `.con`, `.inc` | 5,298 | 18 MB | done | interpreter, [con.md](con.md) |
| `.dds` | 5,246 | 1.4 GB | done for DXT1/3/5 | public format; C++ decoder |
| `.tga` | 3,591 | 223 MB | uncompressed 24/32-bit, 8-bit colour-mapped, 8-bit gray | public format; object lightmaps are colour-mapped |
| `.rs` | 2,619 | 1 MB | done | shader text, [sm.md](sm.md#shader-file-rs) |
| `.sm` | 2,527 | 191 MB | geometry + collision triangles done | footer, collision tree, `alpha` open, [sm.md](sm.md) |
| `.baf` | 1,593 | 8 MB | done | [anim.md](anim.md) |
| `.raw` | 643 | 53 MB | heightmap, material map, path-finding level 0 done | [terrain.md](terrain.md), [pathfinding.md](pathfinding.md); path-finding levels 1+ and `info` open |
| `.tm` | 130 | 3 MB | done | [tm.md](tm.md) |
| `.skn`, `.ske` | 120 / 86 | 1 MB | done | [anim.md](anim.md) |
| `.wav` | 5,808 | 422 MB | standard | RIFF PCM 16-bit in every file; played, [sound.md](sound.md) |
| `.ssc` | 1,801 | 4 MB | partly | [sound.md](sound.md): layers, distance / time / control curves; pitch randomisation, triggers open |
| `.sfk` | 7 | — | not decoded | `SFPK` sound pack index |
| `.lsb` | 36 | 23 MB | not decoded | `LightmapShadowBits.lsb`, per-level lightmap data |
| `.pal` | 75 | — | not decoded | RIFF `PAL ` palettes next to terrain and object lightmaps; lightmap TGAs carry their own palette |
| `.dif`, `.font` | 40 / 2 | 1 MB | `.font` done, `.dif` not decoded | [hud.md](hud.md) |
| `.rcm` | 38 | — | trivial | cube map: INI text listing six face textures |
| `.vso`, `.vs`, `.pso` | 12 | — | not needed | compiled Direct3D 8 shaders; the engine uses its own GLSL |
| `.dat` | 50 | — | not decoded | e.g. `loadcounter.dat` (4 bytes) |
| no extension | 94 | 1 MB | not decoded | menus: `MemeFile 2.0` (DICE serialisation, `dice::meme::*` classes) |
| `.bik` | movies | — | not planned | Bink video, outside the archives |
