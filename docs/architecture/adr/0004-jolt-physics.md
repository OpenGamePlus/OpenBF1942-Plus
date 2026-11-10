# 0004 — Physics: Jolt for rigid bodies, Refractor parts in our code

- Status: Accepted
- Date: 2026-11-10

## Context

Roadmap step 6 needs vehicles. A Refractor vehicle is a tree of parts with their own behaviour (`Spring` wheels, `Engine`, `Wing`, `FloatingBundle`; [vehicles.md](../../formats/vehicles.md)). No library provides those parts, so they are ours in any design. The open choice was who does rigid bodies, contacts and collision queries against the terrain and 1,815 collision meshes: our own code or a library. Writing a robust contact solver is months of work with subtle failure modes (tunnelling, stacking, mesh-vs-mesh).

## Decision

- Jolt Physics (MIT, C++17, CMake) owns bodies, contacts, the terrain height field, static mesh collision and ray casts.
- Our code (`engine/physics.*`) implements the Refractor parts as forces on Jolt bodies: ray-cast spring wheels, engine drive, grip, drag. The handling therefore comes from our code and the template values.
- Jolt is fetched by CMake `FetchContent` at a pinned commit (tag v5.6.0), built static with its own warnings off; no vendored copy in the repo.
- Only `engine/physics.cpp` includes Jolt headers; `physics.h` hides it behind a pimpl.
- The maintainer chose this hybrid on 2026-11-10. Fidelity target (agent default, not yet confirmed): approximate first, calibrate units later by measuring the original game.

## Consequences

- The first configure needs network access (or a pre-filled `FETCHCONTENT_SOURCE_DIR_JOLTPHYSICS`).
- Jolt's default build enables AVX2/FMA etc.; binaries may not run on very old CPUs until those options are pinned for releases.
- A modern solver is steadier than the 2002 engine; quirks of the original (vehicles launched by contacts) may need deliberate emulation.
- Simulation is single-threaded and fixed at 60 Hz for now; Jolt's deterministic mode is available if networking needs it.
