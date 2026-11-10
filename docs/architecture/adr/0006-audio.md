# 0006 — Audio: SDL2 device, our own mixer

- Status: Accepted
- Date: 2026-11-10

## Context

Roadmap step C1 needs sound. Retail sounds are plain RIFF PCM `.wav` (8/16-bit, mono or stereo, 11/22/44 kHz). The `.ssc` sound scripts drive each layer's volume and pitch from distance, time and engine load. No library implements those curves, so that part is ours whatever the choice. OpenAL Soft or miniaudio would add a dependency for 3D panning and resampling, which take about a hundred lines.

## Decision

- SDL2, already linked ([ADR 0003](0003-engine-stack.md)), opens the audio device: 44.1 kHz float stereo through a callback.
- `engine/audio.*` (std only) decodes `.wav`, parses `.ssc` and mixes voices: linear resampling, roll-off beyond `minDistance`, equal-power pan. `engine/sound.*` maps templates to scripts and caches files.

## Consequences

- No new dependency. The mixer runs headless, so tests can check it.
- No HRTF, reverb or Doppler. If those are needed later, a library can replace the mixer behind `Mixer`.
