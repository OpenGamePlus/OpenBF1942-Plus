# PROJECT_CONTEXT

## Identity

Clean-room reimplementation of the Refractor 1 engine (Battlefield 1942, 2002). The game is no longer sold or supported; the goal is to keep it playable and moddable on modern hardware.

## Goals

- Run retail BF1942 content (base, XPack1, XPack2, mods) on a new engine.
- Document every file format so others can build on it.

## Non-goals

- Shipping, recreating or replacing game content.
- Patching or wrapping the original executable (the retail exe still runs; this is a new engine).

## Anti-goals

- Any game data in the repository ([ADR 0001](../architecture/adr/0001-no-game-data.md)).
- Format claims not checked against retail data.

## Stack

| Part | Choice |
|---|---|
| Research tools | Python 3, stdlib only |
| Engine | C++17 / CMake / SDL2 / OpenGL 3.3 ([ADR 0003](../architecture/adr/0003-engine-stack.md)); Jolt Physics ([ADR 0004](../architecture/adr/0004-jolt-physics.md)) |
| Platform | developed on Linux (WSL); target: modern desktop OSes |

## Constraints

- Input data: a retail install at `<BF1942_DIR>`, version 1.61 (`Mods/bf1942/init.con`).
