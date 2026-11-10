# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

## 2026-11-10 — first live test by the maintainer

- They saw the map and heard the shots; the reload animation was almost complete. Two bugs fixed: a shot left a sound looping, because the patches were merged; mouse look needed the right button.
- Patches are now used by role (0 shot, 1 reload, 2 distant, 5 automatic loop with `stop FinishSample`). Playing captures the mouse (F10 frees it). Second test: loops gone, third person fine; the mouse feels inverted (axis unknown; the code turns right with the mouse and looks down with it), the player model flickers, textures look incomplete. Reload animations were cut halfway and are now fitted to `reloadTime`.

## 2026-11-10 — first network recording (E0)

- Elevated `pktmon` sees only LAN broadcasts: the client sends a GameSpy `\status\` to port 22000, and same-machine traffic is loopback. A UDP relay in WSL (a separate host) recorded a full 5-minute session: 14,707 packets. The query protocol on port 23000 is documented.
- Kept out of the repository: the recording (player name, CD-key hash). Next: decode the bit-packed game packets.

## 2026-11-10 — landing craft and ship spawns (roadmap B3)

- Spawn points inside ships (`teamOnVehicle 1` spawners) belong to the spawner's team and move with the ship: 1,936 bots over 78 levels (was 1,689), 5 levels still one-sided (Battle of Britain ×3, Santo Croce, Essen).
- Bots carry passengers in troop boats and land them at the beach (40 m from the flag or aground).

## 2026-11-10 — vehicle seats (roadmap B3)

- Nested `PlayerControlObject`s read as seats: each has its own weapon, mouse-look parts, `SeatObject` position and exit. The player enters the first free seat and changes with F1–F8.
- Bots board land vehicles and drive them to flags on the tank path-finding map. Armed seats aim with the seat's mouse-look and fire. Stuck drivers back out, then give up. Over 78 levels: 718 boardings, captures 57 → 78, bot deaths 368 → 173 (crews resist rifles).
- Seated soldiers drawn per `seatFlags` with the seat animations, pelvis on the `SeatObject` (checked on a Willys). Open: pilots and boats, entry points.

## 2026-11-10 — fire and reload animations (roadmap B2)

- Fire (looping) and reload (play once) upper-body states and the weapon's own `WeaponFire` / `WeaponReload` skeleton animations play for the player (both views) and bots. The upper body has its own speed and start time.
- Open: the state machine's transitions, weapon switch / deploy, recoil and sway.

## 2026-11-10 — first person (roadmap B2)

- Live play starts in first person (V toggles). It uses the original 1p arms and the states' second (1p) animation, hung from the camera by `center1pHands`, and drawn in a last pass with `set1pFov`.
- Open: fire, reload and switch animations; the 0.47 field-of-view reading.

## 2026-11-10 — animation fix and held weapons (roadmap B2)

- `.baf` quaternions are now read conjugated, and biped roots get a quarter turn from Max space instead of being dropped. Soldiers no longer lean back about 28°; legs, height and knees check out. Rejected: our own two-hand barrel rule, which hid the real error.
- Soldiers hold their weapon on its skeleton (`useSkeletonPartAsMain`, `bindToSkeletonPart`, `copyState2` per-weapon arm states); a 180° turn on the main bone was fitted from the left-hand position. Open: first person, reload / fire arm animations.

## 2026-11-10 — parachutes and fall damage (roadmap B2)

- `setParachuteDrag` read as quadratic drag (24 / 100 kg: 6.4 m/s descent). Space while falling opens it, and the model is drawn above the soldier. Landings above 10 m/s hurt; guessed.
- Open: bots take no fall damage; the original's opening rules are unread.

## 2026-11-10 — crouch and prone (roadmap B2)

- The player crouches (ctrl) and lies down (Z). The capsule shrinks, so shots pass over a prone soldier. Speeds are guessed, standing up needs room, and the crouch / lie animation states play.
- Bots crouch (scouts lie down) while fighting and throw grenades at 12–35 m. Over 78 levels in 5 minutes: 302 grenades, bot deaths 258 → 368, flag captures 68 → 57.
- Open: prone capsule upright, posture transitions.

## 2026-11-10 — explosions and kit weapons (roadmap B2)

- Projectile `radius` read: `material2` damage hits everything within it, falling off linearly (unverified); fuses that run out explode. Over 78 levels bot deaths 236 → 258.
- The player carries the whole kit (keys 1–9), each weapon with its own magazines. `dieAfterColl 0` projectiles bounce and rest: grenades on their fuse, mines near vehicles, explosive packs on G. Open: bots using secondary weapons.

## 2026-11-10 — magazines and reload (roadmap B2, first part)

- `magSize`, `numOfMag`, `reloadTime` and `fireOnce` read. Player and bots count rounds, reload on empty or with R; the HUD shows rounds / spare magazines.
- Supply depots read (1,567 over 78 levels): ammunition and health within their radius; bots out of spare magazines walk to one.
- Bot deaths over 78 levels in 5 minutes fell from 494 to 236. Measured: reload pauses cut bot shots by about a third (El Alamein 663 → 445); running dry was not the cause. Kept, as it is the original rule.

## 2026-11-10 — HUD (roadmap C2, first part)

- `.font` decoded (glyph `x0 y x1`, gray TGA type 3 added to the decoder). An on-screen overlay replaces the window-title HUD: crosshair, tickets and flags, health, weapon, vehicle hit points, a feed of kills / captures / wrecks, round result.
- Open: original HUD textures and layout, `.dif` fonts, ammo (needs magazines, roadmap B2), minimap.

## 2026-11-10 — audio (roadmap C1, first part)

- SDL2 device plus our own mixer ([ADR 0006](../architecture/adr/0006-audio.md)); `.ssc` layers with distance / time / control curves; every shot and the driven vehicle's engine play. Templates now record their script folder (relative `loadSoundScript` paths), in both CON interpreters.
- Sweep: every template's script in the three mods parses, every referenced `.wav` decodes; HIGH level chosen because some scripts ship only `High/`.
- Open: soldiers, impacts, explosions, ambient, radio; real engine rpm; not heard by a person yet (built remotely).

## 2026-11-10 — AI navigation (roadmap B1)

- Level-0 path-finding maps decoded (tile markers 0 / 1 / bit tiles, 1 = blocked; all 207 parse, aligned with level objects). Bots route with A* on a 2 m grid: over 78 levels stuck sidesteps −32 %, bot deaths +36 %.
- Goal hysteresis added (bots swapped between equidistant flags every step). Tried the detailed (last) collision block for static meshes: worse on open maps, rejected.
- Open: cities (Berlin, Stalingrad: spawns inside buildings, doorways), levels 1+ of the maps, 13 levels without level-0 maps.

## 2026-11-10 — second network capture attempt (E0)

- Relays on the query port (with game-port rewriting) and the game port saw no packet: the client reaches the server another way. The server's query port follows `game.gameSpyPort`; ASE is game port + 123.
- Config changed for seconds and restored byte-identical again; no process or file left behind.
- Next: one elevated `pktmon` capture with the maintainer present.

## 2026-11-10 — consolidate (roadmap A)

- `mapview.cpp` (~1,400 lines) split into `engine/game.*` (headless game layer), `engine/render.*` (GL) and thin `mapview.cpp`; new `obf_sim` runs the demos without a window; CI on GitHub.
- Proven equivalent: old and new builds give identical `--shot` logs and images on five scenarios — after finding that moving code changed results by 1 ulp: Jolt's `-mfma` reaches our sources, so the compiler fused multiply-adds differently per file. Our code now builds with `-ffp-contract=off` (needed for replays and network sync).
- Open: obf_render still includes game.h for its view types.

## 2026-11-10 — first network session attempt (E0)

- Ran the original server and client on the maintainer's machine (allowed, maintainer remote): Berlin loads, but no packets captured — the client discovers LAN servers and connects to the reported address, bypassing a relay on `+joinServer`'s address.
- The server reads only `Mods/<mod>/Settings/ServerSettings.con`; its port was changed for seconds and restored byte-identical (sha1 checked). No other install file touched.
- Next: relay that also answers the LAN query and rewrites the port, or one elevated capture with the maintainer present.

## 2026-11-10 — roadmap

- Maintainer goal: play with original BF1942 clients and servers (ADR 0005); all phases in scope, order delegated to the agent.
- Order: consolidate the code, start protocol research early (riskiest, shapes the game layer), then AI navigation, audio, HUD, gameplay completeness, fidelity measurements, modes, menus, networking, rendering, distribution.
- Open: maintainer runs the original game for captures (E0) and measurements (D).

## 2026-11-10 — conquest

- `engine/conquest.*`: flags neutralise and capture by who stands in their radius, tickets bleed and drop per death, a team at 0 loses; spawn points follow their flag. Bots on both teams target the nearest visible opponent; window title is the HUD.
- Straight-line bots got stuck behind buildings (11 of 11 still "running" after 5 min); a 2 s stuck check with a 1.5 s sidestep fixed most. A simulated El Alamein game ends after ~32 min.
- Open: path finding, real conquest rules, vehicles for bots, other game modes.

## 2026-11-10 — first AI, control points

- Enemies see (line of sight), turn, fire with aim error, and otherwise run to the nearest flag their team does not hold; the player takes damage, dies and respawns at home.
- Control point instances were missing in both interpreters: `ControlPoints.con` is run bare and re-checks `v_arg1 == host`. A bare `run` now keeps the caller's arguments; C++/Python parity re-checked (78 levels, 0 mismatches).
- Open: path finding, capturing and tickets, AI in vehicles.

## 2026-11-10 — teams, kits, enemy soldiers

- `--play 1|2` takes the team's soldier (`game.setTeamSkin`) and kit primary (`game.setKit`, `--kit`); enemies stand on the other team's spawn points (SpawnPoint group → ControlPoint team) and can be shot dead.
- Soldiers are hittable through a Jolt inner body on their character; killed ones are parked under the world so shots pass.
- Open: AI, spawning on carriers, secondary weapons.

## 2026-11-10 — weapons and damage

- Damage = materialDamage × damageMod(attGroup, defGroup) from the MaterialManager files the engine runs; the target is the armour material of the collision face hit. Sherman shells need 3 front / 2 side / 1 rear hits on a Sherman and 13 on a Tiger's front: the retail armour design shows through.
- `roundOfFire` is rounds per second (first read as seconds: a Garand fired every 3.2 s). Destroyed vehicles switch to their wreck detail level.
- Open: explosion radius, effects, magazines, kits, soldiers as targets.

## 2026-11-10 — parked vehicles are drivable

- Spawner placements that can move become physics vehicles; E gets into the nearest. `build_scene` returns spawn points; `build_object` no longer follows nested spawners.
- Sweep of 1,546 parked vehicles found real defects: spawns under our terrain fell through it (now lifted), a carrier's single convex hull threw its aircraft off (now per-part hulls, big parts as 8 m columns), hard brakes nosed planes over.
- Open: LCVPs on davits, two Salerno Willys, entry points.

## 2026-11-10 — soldier on foot (step 7 starts)

- `--play USSoldier`: Jolt `CharacterVirtual` capsule walking on terrain, buildings and vehicles; lower-body animation follows the movement; E gets into the `--drive` vehicle and out at its `setSoldierExitLocation`.
- Templates hold no soldier speeds; all movement numbers are guesses to calibrate.
- Open: entry points, crouch/prone, weapons, several vehicles to choose from.

## 2026-11-10 — moving parts

- `build_object` takes a joint function (template name + visit index); physics animates rotational bundles from their own input, limits and speeds, wheels from the ground, wing deflections and propellers. Turrets follow the mouse.
- Engine rotation deliberately not applied: car wheels hang under the Engine and would orbit its axis.
- Open: tracks (`AnimatedBundle`), gear retraction, propeller rpm.

## 2026-11-10 — boats

- `FloatingBundle` = buoyant column (lift × submerged fraction); with it LCVP, Hatsuzuki, Prince of Wales, Fletcher, Shokaku, Sub 7C and Elco rest within ~0.3 m of where the retail levels place them — the strongest check so far.
- Water drag at each float pinned long hulls (Fletcher 0.08 °/s): sideways damping now acts at the centre only. Ship thrust read like aircraft (Σ torque) — torque × differential made the PT boat as slow as an LCVP. The viewer stops drawing `createInvisible` parts.
- Open: big ships turn slowly; submarine diving; unused float settings; all 78 maps load with physics (27,630 static bodies).

## 2026-11-10 — aircraft

- Wings push along their normals (angle-of-attack lift + control flap); regulated main wings hold 1 g; thrust = summed engine torque. Parked fighters' retail pitch (−11° to −14°) matches the Mustang's spring rest (14.4°): independent check of the spring reading.
- Rejected readings: regulate 4.91 per wing (Il-2 half lift, SBD 1.5 g); thrust from torque × differential (twins and B-17 never take off). Found: a Spitfire driven from its spawn point started inside the parked static copy and was pushed under the terrain — spawner vehicles are no longer static bodies.
- Open: C-47 (no regulator), XPack2 jets and rockets, gear retraction, calibration.

## 2026-11-10 — tracked vehicles

- Maintainer confirmed the fidelity target: approximate first, calibrate later. Tanks drive: zero-strength rollers carry nothing, the engine's yaw input turns the hull on the spot, running gear is left out of the hull.
- A whole-fleet sweep found the real causes: springs along the tilted body axis cost half the acceleration (now along the ground normal); centre of mass at hull height rolled jeeps in ordinary turns (lowered to wheel height).
- Open: M3A1 and Sexton hulls still touch the ground; turret, track and wheel animation; aircraft and boats.

## 2026-11-10 — physics: Jolt + Refractor wheels

- Maintainer chose the hybrid: Jolt (ADR 0004) for bodies and collision, our ray-cast `Spring` wheels, engine and grip as forces; `--drive Willy` drives a jeep on any map.
- Reading strength/damping per wheel mass makes a Willys rest exactly at its model wheel positions; first run rolled over (grip at ground level, 2 m spawn drop) and crept on slopes — fixed with grip near COM height and an idle hold.
- Open: units of torque/drag (placeholders), tanks/aircraft/boats, wheel animation, calibration against the original.

## 2026-11-10 — object lightmaps

- Per-instance lightmaps decoded: name = mesh file + truncated instance position (67/67 on Wake), 8-bit colour-mapped TGA, second UV of `.sm`.
- First tried ×1 from one bright Wake bunker; measured mean lightmap brightness (0.13–0.21) showed ×2 is needed. Added a format coverage table.
- Open: `palette.pal`, `.lsb` shadow bits, exact blend.

## 2026-11-10 — vehicle structure survey

- `LodObject` children are alternative detail levels; the viewer drew all of them (hull + low-detail + wreck overlapping) — now only the first.
- Documented vehicle trees (Engine, RotationalBundle wheels, Spring suspension, Wing) as research for step 6; parameter units unknown.
- Open: physics approach (own code vs. library) and fidelity target — decision for the maintainer.

## 2026-11-10 — C++ review fixes

- Independent review found three confirmed crashes on malformed mods: `std::regex` stack overflow in `.rs`, self-recursive `run`, NaN frame index; plus template fan-out and unchecked allocation sizes.
- All fixed with regression tests; rules added to boundaries (no regex on file data, budgets on data-driven recursion).
- Open: Unicode whitespace and lone-`if` handling differ slightly from Python on unusual files.

## 2026-11-10 — level look and collision meshes

- Viewer draws the sky box, fog to the map's view distance and sun lighting from `Init.con`; terrain stays unlit (baked).
- Collision blocks decoded to triangles + `MaterialManager` ids (all 1,815 blocks); wireframes overlay the visible geometry exactly.
- Open: the collision tree, the role of a second block, fog start and light scale.

## 2026-11-10 — animated meshes in the C++ viewer

- `engine/anim.*` ports the readers, pose and skinning; the viewer skins control-point flags and a `--soldier` demo with lower + upper body states.
- Biped root kept only as pelvis height; skinned meshes lit with face normals since no bind pose exists.
- Open: frame rate (25 fps guessed), state machine transitions, held weapons, the 20° stance lean.

## 2026-11-10 — skeletons, skins, animations (Python)

- Decoded `.ske`, `.skn` (with bone name table) and `.baf` (per-channel RLE of i16); all 1,799 files parse to their end; skin links to `.sm` vertices by position.
- Conventions fixed visually with a throwaway rasterizer: standard quaternions, `T·R` locals; the conjugate gives impossible knees.
- Open: the Biped root (`Bip01`) is in 3ds Max space — replaced by identity for soldiers; residual 20° stance lean unverified.

## 2026-11-10 — map objects in the viewer

- C++ ports of the CON interpreter (parity with Python on all 78 levels) and `.sm`; new `.tm` format; `scene` places instances, children and spawner vehicles.
- Rotation convention (D3D yaw/pitch/roll) checked against baked building shadows in Berlin; tree sprites are group 3 of `.tm`.
- Rejected: decoding the collision tree to skip it in `.tm` — the geometry is found from the end of the file instead.

## 2026-11-09 — C++ engine start: terrain viewer

- `obf_mapview` (SDL2, OpenGL 3.3, epoxy) renders heightmap, colour tiles and water; all 78 levels open; RFA/LZO/VFS ported with bounds checks.
- Height formula (`value × yScale / 256`, rows along z) fitted on all base maps; tile order picked as the only seamless variant.
- Open: map objects in the viewer need a C++ CON interpreter; `texOffset`, material map and water rules unknown.

## 2026-11-09 — static meshes

- `tools/sm.py` parses all 2,527 `.sm` (v9/v10, triangle lists and strips) and exports OBJ + MTL + textures; geometry checked visually.
- Rejected: writing a renderer for the model viewer — Blender already opens OBJ/DDS; rendering waits for the map viewer.
- Open: collision blocks, mesh footer, the `alpha` field.

## 2026-11-09 — CON interpreter

- `tools/con.py`: VFS over the mod stack + interpreter with an active-object model; all 78 levels load and every placed object resolves to a template.
- Path rule settled from data: script folder, then level folder, then mod root; a test caught `->` targets being substituted.
- Open: the level load sequence and object file order are assumed; no game logic, the props are only recorded.

## 2026-11-09 — documentation system

- Added AI memory layer, ADRs, format specs and `tools/check_docs.py`; English docs, README also in Spanish.
- Rejected: the full template structure (boundaries, glossary, development/) — no content yet.
- Open: boundaries doc when the C++ engine starts.

## 2026-11-09 — CON language survey

- Inventoried all `.con`/`.inc` in retail data: subsystem.method commands with an active-object model, `run`/`include`/`if`/`beginrem`.
- Retail scripts contain junk lines, so the interpreter must ignore unknown lines.
- Open: `run` path resolution; interpreter not started.

## 2026-11-09 — RFA reader

- Decoded RFA layout and implemented LZO1X in pure Python; verified on all 31,254 retail files.
- Rejected: third-party LZO packages (stdlib-only tools).
- Open: meaning of the per-archive `unk` field; patch override order.
