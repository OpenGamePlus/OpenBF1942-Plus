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
| Effects not drawn | not implemented | later steps |
| Spawner team in the viewer | shortcut | instance `setTeam`, else first `setObjectTemplate`; no game state |
| Animation rotations and held weapons | unverified | `.baf` quaternions read conjugated and biped roots turned from Max space, chosen from pose checks; held weapons turned 180° on their main bone, fitted; first-person `set1pFov` read as vertical radians, fire / reload states chosen by the game, not the state machine's transitions; no switch, sway or recoil animation; see [anim.md](../formats/anim.md#held-weapons-unverified) |
| Animation frame rate and state machine | shortcut | 25 fps guessed; first animation of a state, looping; no transitions; weapon states by prefix |
| `animations/animationstates.con` loading | unverified | the viewer runs it after the object folders |
| Object lightmap blend | unverified | 2 × lightmap replaces the sun term on lightmapped meshes |
| Fog, sky, light scale | shortcut | fog uses `renderer.fogStart` / `fogEnd` where the level sets them (15 of 26 checked; otherwise half the view distance to the view distance). The sky is fogged as geometry 250 m away (our rule), which matches both original references: Berlin's sky a uniform pale sepia, El Alamein's clear blue. Mesh light ×2; terrain drawn unlit (baked lighting). First-person arms look larger than in the original (`set1pFov` reading) |
| Look against the original | measured | side by side on Berlin with the original client (2026-11-10). The ground lacked the terrain detail texture; it is now applied, with anisotropic filtering. Terrain brightness is still open. In a second test the camera was matched to the same spot (Berlin, Soviet base, by the tank stoppers and the two tanks). There the original's road measures 1.85 times ours, while buildings (≈1.0) and haze (≈1.1) already match, and the field of view matched ours (vertical ≈1.0 rad). The El Alamein reference (same spot, the Allied base flag facing the factory) settles that terrain brightness already matches there: sand 225 against the original's 213, factory 112 against 123. So no ×2. The Berlin road gap is something else, perhaps fog strength or a Berlin-specific setting; it is still open. Our buildings are grey-blue and darker, the original's warm and sepia. The original's haze starts much closer. Skinned meshes were lit per face (faceted, flickering); now smooth normals are recomputed from the posed mesh |
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
| Physics scope | not implemented | no gears, jets, gear retraction, submarine diving, track animation; trees, animated meshes and stationary spawner objects have no collision; M3A1 and Sexton hulls touch the ground; C-47 does not lift off; big ships turn very slowly |
| Weapons | shortcut | explosion damage falls off linearly to the radius, measured to vehicle centres; bouncing (grenades, mines, packs) with guessed restitution, resting shots do not ride moving vehicles; mines trigger on vehicle centres; only the player places mines and packs; no switch time; no drag, no effects; bots use their primary weapon and grenades only (no pistols, mines, packs); vehicle guns unlimited; reloading drops the rounds left and starts by itself on empty; supply depot rates (`addAmmoType`, `setHealth`) read as per second, depots at their root object's position, no vehicle repair or rearm; armour does not turn with turrets; body part by hit height |
| Bots and AI | shortcut | sight, reaction, aim error, speed, respawn times guessed; routes on the 2 m infantry map, weak in cities (Berlin, Stalingrad); straight runs on the 13 levels without level-0 maps; no vehicles; none on ship spawn points |
| HUD | shortcut | our own layout and colours, not the original HUD textures (`setAmmoBar` images unread); space width guessed; `.dif` fonts unused; no minimap, radio |
| Audio | shortcut | `.ssc` semantics inferred; `randomPlay` variants all play (M1 Garand peaks at 46 voices); volumes above 1 capped; six-param ramps use four; no Doppler, pitch randomisation, triggers; engine load from speed; only shots and the driven engine sound; files load inside the audio lock (a first shot may stutter) |
| Collision blocks | open question | static collision uses the first block of each mesh; the last is more detailed (armour materials); which one the original uses for soldiers is unknown, see [pathfinding.md](../formats/pathfinding.md#use-in-the-engine) |
| Conquest rules | unverified | capture, ticket bleed and win rules read from field names; conquest mode only |
| Parked vehicles | shortcut | lifted onto the terrain; LCVPs on davits drop into the sea; two Salerno Willys fall 50–70 m (unexplained); column hulls approximate concave parts |
| Soldier movement | shortcut | speeds, jump, swim depth guessed; entry by distance, not `EntryPoint`, into the first free seat; half-body seats drawn whole, seat offsets measured on the Willys pose only; bot crews: land and landing craft only (no pilots, no large ships), boarding range, stops, beaching and jam rules guessed; crouch and prone speeds and capsules guessed, prone capsule upright, bot postures and grenade range / rate guessed; parachute key, drag reading and fall damage guessed, bots take none; no ladders, jump or swim animations |
| Moving parts | shortcut | propeller rpm guessed; drawn wheel droop capped at 0.2 m; tracks, hands, gear retraction, engine rotation not drawn |
| Boat model | unverified | water drag rates, ship thrust = Σ torque / 4; float rest heights match retail spawns, see [vehicles.md](../formats/vehicles.md#boats-implemented-unverified) |
| Aircraft model | unverified | wing force constants, regulator as 1 g shared, thrust = Σ torque / 4; see [vehicles.md](../formats/vehicles.md#aircraft-implemented-unverified) |
| Jolt fetched at configure | accepted | network on first configure; default AVX2 build ([ADR 0004](../architecture/adr/0004-jolt-physics.md)) |
