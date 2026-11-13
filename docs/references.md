# External references

Third-party work on Battlefield 1942 that we read, reviewed 2026-11-13. A fact taken from any of them is **unverified** until checked against retail data or our own captures ([AGENTS.md](../AGENTS.md) rule 6).

How each source may be used:

- **Port**: the licence lets us carry code over (GPLv3 combines with our AGPL-3.0, GPLv3 §13); keep its copyright notice and name the source in the commit and in the file.
- **Read**: facts only. Write the format or behaviour in our own words and code; never copy code or paste excerpts.
- Decompiled game code and files derived from the game executables (IDA/Ghidra databases, decompiled functions) are always *read*, never committed, in whole or in part (rule 1). A local copy sits outside the repository at `<RE_DIR>`.

## Sources in use

| Source | Licence | Use | What it gives | Roadmap step |
|---|---|---|---|---|
| [JayBiggsGMG/bf1942reverseengineering](https://github.com/JayBiggsGMG/bf1942reverseengineering) | none; decompiled game code | read | Ghidra output of four builds. The Linux dedicated server and the Mac client keep DICE's symbol names: `dice::ref2::io::BitStream`, `GameServer` ghost manager (`assignGhostId`, `addScopableGhost`, `confirmGhost`), `BFSoldierNetworkable` and the other networkables; damage and material code | E0, E2, D |
| [luccawulf/RefractorForge](https://github.com/luccawulf/RefractorForge) | GPLv3 | port | Map editor. Its Validated_Format_Facts and SM_Collision_RE notes: `LightmapShadowBits.lsb` (run-length coded, re-encoded byte-exact), terrain material 7 counts as outside the world even inside the combat area, COL02 is the collision that stops soldiers and vehicles, the `.sm` BSP tail, the archives each executable mounts, RFA writing. Many facts were measured on BF Vietnam | C5, D, B |
| [keowu/gamespy](https://github.com/keowu/gamespy) | GPLv3 | port | GameSpy 2002 SDK as used by BF1942: master server (TCP 28900, `\basic\secure\` challenge, enctype 2 list encryption, server list layout), server verification through `\status\`, client-side CD-key check. Its IDA databases of the game executables are *read* only. Pair with Luigi Auriemma's enctype papers | E2 |
| [Ahrkylien/BF1942-Extraction-Readout-Scripts](https://github.com/Ahrkylien/BF1942-Extraction-Readout-Scripts) | none | read | `MemeFile 2.0` menu format (type table of 196 node, action and effect types, ImHex pattern, XML round trip); reader of the dedicated server's XML event logs (`scoreEvent`, `spawnEvent` with positions, round start and end). Its test data holds game menu files | C4, D |
| [quartztester/bfmeshview](https://github.com/quartztester/bfmeshview) | AGPL-3.0 set by the archivist; the original (Martijn, bytehazard.com) states no licence | read | BfMeshView's VB6 source: `.sm` and `.tm` readers, the BF1942 `.rs` material flags (lighting, specular, envmap, transparent, two-sided, depth write, alpha test reference) | C5 |
| [Ahrkylien/BF1942-BSP-tree-generator](https://github.com/Ahrkylien/BF1942-BSP-tree-generator) | MIT, but reconstructed from DICE's `MakeBSP.exe` 1.7 | read | How the BSP tree in `.sm` collision blocks is built | only if we write `.sm` |
| [c0d3m4nc3r/OpenField42](https://github.com/c0d3m4nc3r/OpenField42) | GPLv3 | port | Another open BF1942 engine (renderer only). Shader changes said to match Refractor: fog colour quantised to 8 bits, D3D8 directional light, terrain detail ×128 | C5 |

## Reviewed, not used

| Source | Why not |
|---|---|
| [c0d3m4nc3r/librfa](https://github.com/c0d3m4nc3r/librfa) | read-only RFA extractor; we already have one |
| [luccawulf/RefractorForgeLivery](https://github.com/luccawulf/RefractorForgeLivery) | skin editor on RefractorForge's libraries; nothing beyond them |
| [luccawulf/RefractorForgeArchive](https://github.com/luccawulf/RefractorForgeArchive) | RFA browser on RefractorForge's libraries; nothing beyond them |
