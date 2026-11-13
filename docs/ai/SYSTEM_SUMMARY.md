# SYSTEM_SUMMARY

Last updated: 2026-11-10

## Tree

```
AGENTS.md            agent entry point
CMakeLists.txt       engine build (libraries obf, obf_render; obf_mapview, obf_sim, test_engine)
.github/workflows/   CI: build, engine tests, tool tests, doc checks
README.md            English; README.{es,pt,fr,de,ru}.md translations; LICENSE AGPL-3.0
docs/                formats/, architecture/adr/, roadmap/, ai/
engine/              C++ engine: rfa, vfs, dds, terrain, con, sm, tm, anim, scene, physics, damage, conquest, nav, audio, sound, hud, game, render, math.h, mapview.cpp, sim.cpp, test_engine.cpp
tools/rfa.py         RFA archive reader (list, extract)
tools/con.py         CON interpreter + virtual file system (mod/level loader)
tools/sm.py          static mesh reader, .rs shader parser, OBJ export
tools/tm.py          tree mesh reader
tools/anim.py        skeleton, skin and animation readers, pose and skinning reference
tools/pathfinding.py AI path-finding map reader (level 0)
tools/bfnet.py       game traffic: transport reader, recording relay, session summary
tools/check_docs.py  documentation checker
tools/test_*.py      unit tests (synthetic data only)
```

## State per subsystem

| Subsystem | State | Reference |
|---|---|---|
| RFA archives | done, verified on the full retail install | [rfa.md](../formats/rfa.md) |
| CON scripts | interpreter done; all 78 levels load with every placed object resolved | [con.md](../formats/con.md) |
| Static meshes (`.sm`, `.rs`) | done; all 2,527 meshes parse, OBJ export with textures; collision triangles + materials decoded, drawn with `--collision` | [sm.md](../formats/sm.md) |
| Tree meshes (`.tm`) | done; all 130 parse (Python + C++), drawn with sprite billboards | [tm.md](../formats/tm.md) |
| Textures (`.dds`/`.tga`) | C++: DDS DXT1/3/5, uncompressed TGA (24/32-bit, colour-mapped, gray); anisotropic filtering; cut-outs tested at full resolution | — |
| Terrain | heightmap, colour tiles, detail texture (`detailTexName`), water plane, sky box, per-level fog (`renderer.fogStart/End`), sky fogged as at 250 m, sun light (`sunLightDirectionVec`, x and z negated); all 78 levels open. Matched against the original client on Berlin and El Alamein except Berlin's street brightness | [terrain.md](../formats/terrain.md) |
| Map objects in the viewer | static and tree meshes, child templates, spawned vehicles, animated meshes, object lightmaps; first LOD only, no effects | [con.md](../formats/con.md#object-placement) |
| Skeletons, skins, animations | Python + C++. `.baf` quaternions conjugated, biped roots turned from Max space. CPU skinning with smooth posed normals; state animations, per-weapon arm states (`copyState2`), fire / reload (fitted to `reloadTime`), held weapons on their skeleton, first-person arms, seated soldiers | [anim.md](../formats/anim.md) |
| Physics + vehicles | Jolt world: terrain height field, static `.sm` collision; drivable cars, tanks, aircraft and boats (`--drive`), handling uncalibrated | [vehicles.md](../formats/vehicles.md), [ADR 0004](../architecture/adr/0004-jolt-physics.md) |
| Weapons and damage | vehicle guns from their posed barrel, the soldier kit (keys 1–9) with magazines and reload; ballistic projectiles; damage from MaterialManager tables and the armour face hit; explosions with radius; supply depots; wrecks | [weapons.md](../formats/weapons.md) |
| Soldier on foot | `--play 1` or `2`: team soldier with the whole kit, first / third person, crouch / prone, parachute and fall damage; bots of both teams (see, shoot, crouch, throw grenades, run for flags on path-finding routes, board and crew vehicles); conquest flags and tickets (`engine/conquest.*`), ship spawn points, deaths and respawns | [vehicles.md](../formats/vehicles.md#soldiers-on-foot-implemented-unverified) |
| Vehicle crews | seats from nested `PlayerControlObject`s (weapon, mouse-look parts, `SeatObject`, exit); player F1–F8; bot drivers on the tank / landing-craft maps, gunners aiming through the seat's look inputs, troop boats that land their passengers | [vehicles.md](../formats/vehicles.md), [weapons.md](../formats/weapons.md#ai-implemented-placeholder) |
| Audio | SDL2 device, own mixer; `.ssc` patches by role (shot, reload, distant, automatic loop), driven engines | [sound.md](../formats/sound.md), [ADR 0006](../architecture/adr/0006-audio.md) |
| HUD | `.font` text: crosshair, tickets and flags, health, weapon and rounds, vehicle and seat, kill / capture feed | [hud.md](../formats/hud.md) |
| Network (research) | one original client/server session recorded through a WSL relay; GameSpy LAN discovery and query documented; game transport decoded (`tools/bfnet.py`); payload sections and ghost entries read from the Mac client decompilation, not yet checked on traffic; game events open | [network.md](../formats/network.md) |
| Engine (C++) | `obf` (game, headless) + `obf_render` (GL) libraries; `obf_mapview` (window), `obf_sim` (headless runs) | [boundaries.md](../architecture/boundaries.md) |

## Commands

| Task | Command |
|---|---|
| Tests | `python3 -m unittest discover tools` |
| Build engine | `cmake -S . -B build && cmake --build build -j` |
| Engine tests | `ctest --test-dir build` |
| Map viewer | `build/obf_mapview <BF1942_DIR> <mod> <level> [--shot out.ppm] [--cam x y z yaw pitch] [--mode conquest|ctf|tdm|coop] [--soldier <template>] [--drive <vehicle>] [--play <team or soldier>] [--kit 0-4] [--sim s] [--fire-ahead] [--collision] [--no-objects]` |
| Headless game run | `build/obf_sim <BF1942_DIR> <mod> <level> (--play <team or soldier> [--kit 0-4] \| --drive <vehicle> [--fire-ahead]) [--cam x y z yaw pitch] [--sim s]` — the `--shot` demo without a window |
| Doc checks | `python3 tools/check_docs.py` |
| List archive | `python3 tools/rfa.py list <archive.rfa>` |
| Extract archive | `python3 tools/rfa.py extract <archive.rfa> <out_dir>` |
| Load mod + level | `python3 tools/con.py <BF1942_DIR> <mod> [level] [mode]` |
| Export mesh | `python3 tools/sm.py <BF1942_DIR> <mod> standardMesh/<name> <out_dir>` |
| Record game traffic | `python3 tools/bfnet.py record <port> <server_ip> <server_port> <out.jsonl> <seconds>` (holds personal data: never commit) |
| Summarise a recording | `python3 tools/bfnet.py summary <in.jsonl>` |

## Contracts

- `rfa.read_dir(data) -> (compressed: bool, [(name, stored_size, size, offset)])`; `rfa.read_index(f)` same from an open file, reading only the directory.
- `rfa.read_file(data, compressed, size, offset) -> bytes` — raises `ValueError` on size mismatch.
- `rfa.safe_path(out_dir, name)` — raises `ValueError` if an archive name escapes `out_dir`.
- `con.load_mod(game_dir, mod) -> Con`; `con.load_level(vm, level, mode="conquest")`.
- `Con.templates[subsystem][lower name] = {type, name, props}`; `Con.instances = [{template, props}]`; `props = [(method, args)]`, method lower-cased.
- `Vfs.files`: lower-cased path -> bytes or archive pointer; `Vfs.read(path) -> bytes`.
- `sm.parse(bytes) -> {version, bbox, collision: [bytes], lods: [[{name, prim, fvf, stride, alpha, vertices, indices}]], end}`; vertices are float tuples in file order.
- C++ (`namespace obf`): `lzo1x_decompress`, `rfa_index`, `rfa_read`; `Vfs::mount_mod/add/has/read/paths/key`; `dds_decode`, `tga_decode -> Image{w,h,rgba}`; `parse_terrain_con -> TerrainInfo`; `load_heightmap -> Heightmap::height(row, col)`; `Con::load_game/load_level/run/execute` with `templates`, `instances`, `settings`; `sm_parse` (with `collision`), `collision_parse`, `sm_triangles`, `rs_parse`; `tm_parse`; `ske_parse`, `skn_parse`, `baf_parse`, `pose`, `skin_map`, `skin_apply` (`pose` also takes one frame per animation); `build_scene(con, vfs, skipped, hide, spawns) -> + [SpawnPoint{name, world}]`, `build_object(con, vfs, name, world, skipped, joint, hide, wreck) -> [DrawItem{mesh, world, skin, skeleton, states, lightmap, owner, spawned, spawn, bone}]`; `vehicle_def(con, vfs, name) -> VehicleDef`; `Physics(hm, world_size, water_level)` with `add_scene`, `add_static`, `add_vehicle`, `drive(v, throttle, yaw, pitch, roll)`, `look(v, x, y, seat)`, `VehicleDef::seats`, `joint(v, name, occurrence)`, `set_velocity`, `fire -> id`, `shot_position`, `explode`, `add_character`, `walk`, `place_character`, `set_character_active`, `set_posture`, `open_parachute`, `parachute_open`, `take_landing`, `character_position`, `character_on_ground`, `fire`, `take_hits`, `projectiles`, `kill_character`, `line_of_sight`; `weapon_def(con, name)`; `damage_table(commands) -> DamageTable::damage(attacker, target)`; `conquest_from(con) -> Conquest{flags, tickets, update(dt, present), death(team), winner}`; `nav_parse(data, world_size) -> NavGrid`, `nav_coarse(grid, factor)`, `nav_path(grid, ax, az, bx, bz) -> [x, z]`; `pathfinding.parse(bytes) -> (n, grid)`; `wav_parse`, `ssc_parse(path, read, level) -> SoundPatch`, `Mixer(rate).play/update/release/mix`, `Sounds(vfs, con, rate).shot/engine/script`; `Game(vfs, con, hm, info, scene, spawns, GameOptions, Camera, objects)` with `step(t, dt, GameInput)`, `sounds` (shots this step), `driving(pos, control)`, `demo(sim_seconds)`, `pose()`, `soldiers` (SoldierView), `cam`, `hud_info() -> HudInfo`; `Ammo::full(weapon)`, `take`, `reload`, `tick`, `supply`; `font_parse`, `hud_text`, `hud_layout(font, info, w, h) -> [HudQuad]`; `Renderer(vfs, con, hm, info, scene, objects).draw(scene, cam, w, h, seconds, collision, projectiles, soldiers)`, `.hud(quads, w, h, font_texture)`; `step`, `transform`, `speed`. All throw `std::runtime_error` on bad data.
- `anim.parse_ske/parse_skn/parse_baf(bytes)`; `anim.pose(skeleton, [baf], frame, biped_root) -> {bone: 4x4}`; `anim.skin(vertices, records, names, world) -> [pos]`.
- `tm.parse(bytes) -> {bbox, groups: [[(first, count, texture)] x 3], vertices, indices}`.
- `sm.parse_collision(block) -> {vertices: [(x, y, z, material)], faces: [(a, b, c, material)], tree}`.
- `sm.triangles(material) -> [(a, b, c)]` for lists and strips; `sm.parse_rs(text) -> {material lower: {shader, key: value}}`.
