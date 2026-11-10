# OpenBF1942-Plus

[Español](README.es.md)

Clean-room reimplementation of the Refractor 1 engine so Battlefield 1942 runs on modern systems.

**No game data is included or redistributed.** The engine reads assets from your own installation, referred to as `<BF1942_DIR>`.

## Status

| Step | State |
|---|---|
| 1. Archives (RFA) | done — list/extract, verified on the full retail install |
| 2. Scripts (`.con`) | done — interpreter; every level of the three mods loads |
| 3–8 | not started (see [roadmap](#roadmap)) |

## Tools

Python 3, standard library only.

```
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # load a level, print summary
python3 -m unittest discover tools     # tests
python3 tools/check_docs.py            # documentation checks
```

## Roadmap

1. **Archives (RFA)** — read every game file.
2. **Scripts (`.con`)** — interpreter + object template system.
3. **Static meshes (`.sm`) + textures (`.dds`/`.tga`)** — model viewer.
4. **Terrain** — map viewer: walk around a map.
5. **Skinned meshes + animations (`.skn`/`.ske`/`.baf`)** — soldiers.
6. **Physics + vehicles** — drive a jeep.
7. **Gameplay rules, weapons, AI** — single-player match.
8. **Networking** — original protocol undocumented.

## Documentation

[docs/README.md](docs/README.md) — formats, architecture decisions, debt. AI assistants: start at [AGENTS.md](AGENTS.md).
