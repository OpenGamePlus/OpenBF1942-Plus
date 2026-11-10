# PROJECT_MEMORY

Append-only. Newest first. At most 3 bullets per stage.

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
