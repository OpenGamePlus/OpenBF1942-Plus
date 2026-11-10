# 0002 — Python stdlib for tools, C++ for the engine

- Status: Accepted
- Date: 2026-11-09

## Context

Format discovery needs fast iteration; a real-time 3D engine needs native performance. Both Python 3 and a C++ toolchain with CMake are available.

## Decision

- Research tools (`tools/`): Python 3, standard library only — no install step, nothing to audit.
- Engine: C++ with CMake, started when the first viewer is built.

## Consequences

- Formats get implemented twice (Python reference, C++ engine); the Python tools and `docs/formats/` are the reference the C++ code is checked against.
- Pure-Python LZO is slow; accepted for tools, see [debt](../../roadmap/debt.md).
