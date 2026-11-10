# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

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
