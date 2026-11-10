# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

## 2026-11-09 — C++ engine start: terrain viewer

- `obf_mapview` (SDL2, OpenGL 3.3, epoxy) renders heightmap, colour tiles and water; all 78 levels open; RFA/LZO/VFS ported with bounds checks.
- Height formula (`value × yScale / 256`, rows along z) fitted on all base maps; tile order picked as the only seamless variant.
- Open: map objects in the viewer need a C++ CON interpreter; `texOffset`, material map and water rules unknown.

## 2026-11-09 — static meshes

- `tools/sm.py` parses all 2,527 `.sm` (v9/v10, triangle lists and strips) and exports OBJ + MTL + textures; geometry checked visually.
- Rejected: writing a renderer for the model viewer — Blender already opens OBJ/DDS; rendering waits for the map viewer.
- Open: collision blocks, mesh footer, the `alpha` field.

## 2026-11-09 — CON interpreter

- `tools/con.py`: VFS over the mod stack + interpreter with an active-object model; all 78 levels load and every placed object resolves to a template.
- Path rule settled from data: script folder, then level folder, then mod root; a test caught `->` targets being substituted.
- Open: the level load sequence and object file order are assumed; no game logic, the props are only recorded.

## 2026-11-09 — documentation system

- Added AI memory layer, ADRs, format specs and `tools/check_docs.py`; English docs, README also in Spanish.
- Rejected: the full template structure (boundaries, glossary, development/) — no content yet.
- Open: boundaries doc when the C++ engine starts.

## 2026-11-09 — CON language survey

- Inventoried all `.con`/`.inc` in retail data: subsystem.method commands with an active-object model, `run`/`include`/`if`/`beginrem`.
- Retail scripts contain junk lines, so the interpreter must ignore unknown lines.
- Open: `run` path resolution; interpreter not started.

## 2026-11-09 — RFA reader

- Decoded RFA layout and implemented LZO1X in pure Python; verified on all 31,254 retail files.
- Rejected: third-party LZO packages (stdlib-only tools).
- Open: meaning of the per-archive `unk` field; patch override order.
