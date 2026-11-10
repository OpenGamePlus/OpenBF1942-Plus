# 0001 — Read assets from the user's install, never redistribute

- Status: Accepted
- Date: 2026-11-09

## Context

Battlefield 1942 is no longer sold or supported. The engine code can be reimplemented; the game content (maps, models, textures, sounds, scripts) is copyrighted.

## Decision

The project ships only original code and documentation. All game data is read at runtime from a retail install (`<BF1942_DIR>`), following the OpenMW / OpenRA model.

## Consequences

- No extracted files, archives or executables in the repo; `out/` is git-ignored.
- Tests use synthetic data; checks against retail data run locally only.
- Format docs describe structure, not content.
