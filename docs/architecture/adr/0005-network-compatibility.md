# 0005 — Interoperate with the original game over the network

- Status: Accepted
- Date: 2026-11-10

## Context

The maintainer's goal is to play with original Battlefield 1942 clients and servers, not only between copies of this engine. The original network protocol is undocumented. An original client simulates locally and takes object state from the server, so a compatible server must reproduce the original's object identities, replicated state and game rules closely enough that clients stay in sync.

## Decision

- Network compatibility with the original client and server is a project goal (roadmap phase E).
- The protocol is studied from traffic recorded on the maintainer's own machine between original programs, for interoperability only; findings go to a new network format spec under docs/formats with a decoder and tests on synthetic packets. No captured sessions, keys or binaries are committed ([ADR 0001](0001-no-game-data.md)).
- The game layer is organised around replicable objects matching the data's `NetworkableInfo` templates, so replication can follow the original's object model.
- Fidelity to the original simulation (roadmap phase D) is a prerequisite for mixed games, no longer optional polish.

## Consequences

- Protocol research starts early (roadmap E0) and runs alongside gameplay work.
- Values currently guessed (handling, AI-independent rules, timings) must be measured against the original before mixed games.
- If the protocol proves infeasible (encryption, anti-cheat), the engine keeps its own protocol (E1) and this ADR is superseded.
