# CON script language

Status: **survey only** — inventory of all `.con`/`.inc` files in the retail archives; no interpreter yet.

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

## Tolerance

Retail data contains junk lines (`***`, `-`, typos like `wrem`, `srem`, a UTF-16 BOM). The interpreter must ignore unknown lines without aborting.

## Entry points (partial)

- `Mods/<mod>/init.con` — mod identity, music, mod paths (loose file, not in an archive).
- `bf1942/game/Startup.con`, `bf1942/game/Init.con` — game start (in `Game.rfa`).
- `bf1942/levels/<Map>/Init.con` — level; runs `Init/Terrain`, `Init/SkyAndSun`, game-mode folders (`Conquest/`, `Ctf/`, `SinglePlayer/`).

Open: relative `run` paths resolve against the script folder or the mod root — both forms appear, not yet verified.
