# CON script language

Status: **interpreter working** — `tools/con.py` loads all 78 levels of bf1942, XPack1 and XPack2; every placed object resolves to a defined template.

`.con` scripts drive the engine: object templates, map object placement, game rules, renderer settings. One command per line.

## Command form

```
<Subsystem>.<method>[.<sub>] arg1 arg2 ...
ObjectTemplate.create Flag BlueFlag      # creates and selects the "active" template
ObjectTemplate.team 1                    # applies to the active template
Object.absolutePosition 770.44/95/971.44 # vectors: x/y/z
```

- Subsystem names are case-insensitive (`game.setKit` and `Game.setViewDistance` both appear). Methods: assumed case-insensitive, unverified.
- `create`/`active` select the current object of a subsystem; later lines modify it.

Most used subsystems (line counts): `Object` 152k, `ObjectTemplate` 144k, `MaterialManager` 61k, `GeometryTemplate` 20k, `AITemplatePlugin` 7.6k, `AIStrategicArea` 6.7k, `AnimationStateMachine` 6.2k, `Game` 4.9k.

## Keywords

| Keyword | Use |
|---|---|
| `rem` | line comment; also glued forms like `rem*****` |
| `beginrem` / `endrem` | block comment |
| `run <script> [args]` | run another script; `.con` extension optional |
| `include <file> [args]` | same with explicit extension (`.inc`) |
| `var v_x` | declare variable |
| `v_arg1..N` | arguments of the current `run`/`include` |
| `if v == X` / `else` / `endIf` | conditional |
| `echo` | prints; used as `echo v_arg1 -> v_name` |
| `cmd ... -> v_x` | stores the command result in `v_x`; the target is a name, never substituted |

## Tolerance

Retail data contains junk lines (`***`, `-`, typos like `wrem`, `srem`, a UTF-16 BOM). The interpreter must ignore unknown lines without aborting.

## Path resolution (verified)

`run`/`include` targets are case-insensitive, `\` or `/`, `..` allowed. Tried in order, first hit wins:

1. folder of the running script
2. folder of the current level (`bf1942/levels/<Map>/`), used by `GameTypes/*.con`
3. mod root

Checked on every `run`/`include` in the three mods; the remaining misses are files absent from retail data (optional `*Mod.inc` hooks, typos, Coral Sea control points).

## Object placement

How `Object.create` instances become meshes (`engine/scene.cpp`):

| Step | Rule | Verified |
|---|---|---|
| Instance transform | `T(absolutePosition) · R(rotation) · S(geometry.scale)` | visually (Berlin, Wake) |
| Rotation | `yaw/pitch/roll` in degrees, Direct3D left-handed order: roll (z), pitch (x), yaw (y) | visually: L-shaped corner buildings match baked shadows |
| Geometry | `ObjectTemplate.geometry` → `GeometryTemplate` (`StandardMesh` → `standardMesh/<file>.sm`, `TreeMesh` → `treeMesh/<file>.tm`); level folder first | yes, 0 errors on all levels |
| Children | `addTemplate X`; the following `setPosition`/`setRotation` place that child relative to the parent | visually: tanks and the B-17 assemble correctly |
| LOD | `LodObject` children are detail levels (complex, simple, wreck) | the viewer draws the first child only; `lodSelector` distances not used |
| Spawners | `ObjectSpawner` template: `setObjectTemplate <team> <template>`; the instance's `setTeam` picks one | the viewer shows the instance's team, else the first listed; in game the control point owner decides |

| Animated meshes | `AnimatedMesh` geometry = `standardMesh/<file>.sm` + `setSkin`; skeleton (`createSkeleton`) and states (`setAnimationState`) come from the owning template or a parent | flags wave on control points; soldiers via `--soldier` |

Not drawn yet: effects, weapons held by soldiers.

Parity: `engine/con.cpp` and `tools/con.py` produce the same instance count on all 78 levels.

## Mods

`Mods/<mod>/init.con` (loose file) lists `game.addModPath` entries; the first has the highest priority (XPack1: `Mods/XPack1/`, then `Mods/BF1942/`).

## What the engine loads by itself

No script runs these; `tools/con.py` reproduces them:

| Step | Scripts | Verified |
|---|---|---|
| Game | `bf1942/game/Init.con` | yes |
| Object templates | every `objects/**/` folder: `Geometries.con`, `Network.con`, `Physics.con`, `Weapons.con`, `Effects.con`, `Objects.con` | folders yes, order assumed |
| Level | `<Map>/Init.con`, `<Map>/StaticObjects.con`, game mode (`GameTypes/<mode>.con`, else `<mode>.con`) with arg `host` | sequence assumed |
