# SYSTEM_SUMMARY

Last updated: 2026-11-09

## Tree

```
AGENTS.md            agent entry point
README.md            English; README.es.md Spanish translation
docs/                formats/, architecture/adr/, roadmap/, ai/
tools/rfa.py         RFA archive reader (list, extract)
tools/con.py         CON interpreter + virtual file system (mod/level loader)
tools/check_docs.py  documentation checker
tools/test_*.py      unit tests (synthetic data only)
```

## State per subsystem

| Subsystem | State | Reference |
|---|---|---|
| RFA archives | done, verified on the full retail install | [rfa.md](../formats/rfa.md) |
| CON scripts | interpreter done; all 78 levels load with every placed object resolved | [con.md](../formats/con.md) |
| Meshes, textures, terrain, animation | not started | — |
| Engine (C++) | not started | — |

## Commands

| Task | Command |
|---|---|
| Tests | `python3 -m unittest discover tools` |
| Doc checks | `python3 tools/check_docs.py` |
| List archive | `python3 tools/rfa.py list <archive.rfa>` |
| Extract archive | `python3 tools/rfa.py extract <archive.rfa> <out_dir>` |
| Load mod + level | `python3 tools/con.py <BF1942_DIR> <mod> [level] [mode]` |

## Contracts

- `rfa.read_dir(data) -> (compressed: bool, [(name, stored_size, size, offset)])`; `rfa.read_index(f)` same from an open file, reading only the directory.
- `rfa.read_file(data, compressed, size, offset) -> bytes` — raises `ValueError` on size mismatch.
- `rfa.safe_path(out_dir, name)` — raises `ValueError` if an archive name escapes `out_dir`.
- `con.load_mod(game_dir, mod) -> Con`; `con.load_level(vm, level, mode="conquest")`.
- `Con.templates[subsystem][lower name] = {type, name, props}`; `Con.instances = [{template, props}]`; `props = [(method, args)]`, method lower-cased.
- `Vfs.files`: lower-cased path -> bytes or archive pointer; `Vfs.read(path) -> bytes`.
