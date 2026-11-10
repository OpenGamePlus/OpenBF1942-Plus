# AI path-finding maps

Status: **level 0 decoded and verified (all 207 level-0 maps of the three mods parse to their end; El Alamein and Berlin line up with the level's objects); levels 1+ and `<type>.raw` / `<type>info.raw` not decoded.** Reader: `tools/pathfinding.py`, `engine/nav.*`.

## Files

Per level in `<Map>/Pathfinding/`, one set per search map declared in `AIPathFinding.con` (`ai.addSearchMap <name> <waterHeight> <waterDepth> <maxSlope> <brush> <lowClip> <hiClip> <considerAITypes>`, parameters named in the file's own comment): `Infantry1`, `Tank0`, `Car4`, `Boat2`, `LandingCraft3` (names vary by level). 13 levels (e.g. Aberdeen, Coral Sea, Liberation of Caen, Invasion of the Philippines) ship no level-0 map.

| File | Contents |
|---|---|
| `<name>level0map.raw` | full-resolution blocked map, below |
| `<name>level<k>map.raw`, k ≥ 1 | coarser levels; header `a, a, b, k`, a different tile encoding (not decoded) |
| `<type>.raw` (16,392 bytes) | header `32, 32`, 32 × 32 records of 16 bytes (not decoded) |
| `<type>info.raw` | header as level 0 with `k` = 1 (not decoded) |

## Level 0

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 a, a | 2^a tiles per side |
| 8 | u32 b | 2^b cells per tile side (a + b = 11: 2,048 cells; 12 on 4 km maps) |
| 12 | u32 | level, 0 |
| 16 | u32 × 4 | 0, 2, 0, 0xFFFFFFFF (unknown) |
| 32 | tiles | 2^a × 2^a, row-major (z rows, x columns) |

Each tile starts with a u32 marker: `0` all free, `1` all blocked, `0xFFFFFFFF` followed by 2^b rows of 2^b / 32 u32 words, one bit per cell, bit 0 the lowest x, `1` blocked. Cells cover the terrain: cell size = `worldSize` / cells per side (1 m on every retail level). Blocked covers steep ground, buildings, rocks and water (Wake: 96 % for infantry, an island in a 2 km square).

## Use in the engine

Bots plan on the infantry map at 2 m (a coarse cell is blocked when more than half of its 2 × 2 cells are), A* with 8 neighbours and no corner cutting, then string pulling. 5-minute games on all 78 levels: stuck sidesteps 38,592 → 26,154, bot deaths 364 → 494 (more fights), flag captures about equal. Still weak on cities (Berlin, Stalingrad): spawn points inside buildings and doorways narrower than 2 m.

Tried and rejected: static collision from the last (detailed) collision block instead of the first. It helped Stalingrad slightly but tripled stuck counts on open maps (El Alamein 33 → 474), so the first block stays.
