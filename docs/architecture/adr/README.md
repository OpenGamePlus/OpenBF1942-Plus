# Architecture Decision Records

Accepted ADRs are never edited; a later ADR marks them `Superseded by NNNN`.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-no-game-data.md) | Read assets from the user's install, never redistribute | Accepted |
| [0002](0002-python-tools-cpp-engine.md) | Python stdlib for tools, C++ for the engine | Accepted |
| [0003](0003-engine-stack.md) | Engine stack: C++17, CMake, SDL2, OpenGL 3.3 via epoxy | Accepted |
| [0004](0004-jolt-physics.md) | Physics: Jolt for rigid bodies, Refractor parts in our code | Accepted |

## Template (MADR-lite)

```
# NNNN — Title

- Status: Proposed | Accepted | Superseded by NNNN
- Date: YYYY-MM-DD

## Context
## Decision
## Consequences
```
