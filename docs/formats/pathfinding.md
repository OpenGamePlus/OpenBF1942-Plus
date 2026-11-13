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

Bots plan on the infantry map at 2 m (a coarse cell is blocked when more than half of its 2 × 2 cells are), A* with 8 neighbours and no corner cutting, then string pulling. 5-minute games on all 78 levels: stuck sidesteps 38,592 → 26,154, bot deaths 364 → 494 (more fights), flag captures about equal. Cities (Berlin, Stalingrad) were weak because bots start or end up in cells the map marks blocked: Berlin's Soviet base has spawn points inside blocked footprints, and 111 of 134 stuck events there happened in blocked cells. From a blocked cell, A* snapped to the nearest free cell and the bot walked into the wall between. Now the bot first heads for the nearest free cell it can see (a physics ray at eye height, within 15 m), then follows the path from there. 5-minute games: Berlin stuck 134 → 72, Stalingrad 204 → 91; all 78 levels in 2 minutes: 7,010 → 6,427, captures 51 → 54. A 1 m planning grid was tried: better on Gazala, Tobruk and Battleaxe, worse on both cities, not kept. Also tried and rejected (2026-11-14): opening blocked cells next to free ones where five vertical rays at 0.4–1.6 m hit nothing. With slopes up to ~40° it opened ~80,000 cells on open maps; limited to flat ground, ~1,000–6,000 per level. Either way Stalingrad (63 → 227–258) and Battleaxe (1,065 → 1,493–1,695 stuck) got worse and Berlin did not gain: the rays miss low rubble and thin fences that physics still blocks. Also rejected (2026-11-14): a bot stuck in a blocked cell walking back to its last free position and replanning from there. Captures unchanged over 78 levels (52 → 51) while stuck events doubled (6,437 → 13,627, counting the walks back): the new path led into the same trap.

Static collision uses the last (detailed) collision block (2026-11-13). Tried and rejected on 2026-11-10, when it tripled stuck counts on open maps (El Alamein 33 → 474). Re-measured on 2026-11-13, 5-minute games, first → last block: El Alamein 39 → 29, Gazala 631 → 451, Battleaxe 1,468 → 1,064, Stalingrad 360 → 204, Tobruk 201 → 244, Wake 141 → 153, Berlin 111 → 134. Over all 78 levels in 2 minutes: 9,810 → 7,010. The original stops movement with the last block too ([sm.md](sm.md#collision-block)).
