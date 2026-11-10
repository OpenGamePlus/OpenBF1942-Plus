# 0003 — Engine stack: C++17, CMake, SDL2, OpenGL 3.3 via epoxy

- Status: Accepted
- Date: 2026-11-09

## Context

The map viewer is the first part that needs real-time rendering. ADR 0002 fixed C++ and CMake. The development machine runs Linux under WSL2 (WSLg display); targets are modern desktop OSes.

## Decision

- C++17, CMake ≥ 3.20, one static library `obf` (engine modules) plus executables.
- SDL2 for window, input and GL context; OpenGL 3.3 core; libepoxy as GL loader.
- Textures decoded on the CPU (DXT1) and uploaded as RGB, so no dependency on S3TC driver support.
- Tests: plain `assert` executables registered with CTest, synthetic data only.

## Consequences

- All three libraries are cross-platform and packaged on Linux; no vendored code yet.
- More GPU memory than compressed upload (16 tiles × 3 MB per 2048 m map); acceptable until it is not.
- Little-endian hosts assumed (`memcpy` of file integers).
