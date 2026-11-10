# ARCHITECTURE_NOTES

## Must

- Treat archive contents as untrusted input: validate sizes, refuse path escapes (`rfa.safe_path`).
- Keep each format's reference implementation in `tools/` and its spec in `docs/formats/`.
- Use synthetic data in tests.
- Tools build on each other one way: `rfa` ← `con` (VFS) ← `sm`.

## Must not

- Commit game data or personal paths ([ADR 0001](../architecture/adr/0001-no-game-data.md)).
- Add third-party Python dependencies to `tools/` ([ADR 0002](../architecture/adr/0002-python-tools-cpp-engine.md)).
- Abort on unknown `.con` lines: retail scripts contain junk.

## Irreversible decisions

| Decision | ADR |
|---|---|
| No game data, read the user's install | [0001](../architecture/adr/0001-no-game-data.md) |
| Python tools, C++ engine | [0002](../architecture/adr/0002-python-tools-cpp-engine.md) |

Layer boundaries get their own boundaries document under docs/architecture once the C++ engine exists.
