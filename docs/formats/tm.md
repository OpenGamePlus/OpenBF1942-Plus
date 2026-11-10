# TM tree mesh format

Status: **verified** — `tools/tm.py` and `engine/tm.cpp` parse all 130 `.tm` files in bf1942, XPack1 and XPack2; rendered on Wake (palms) and Battle of the Bulge (pines with sprites).

Little-endian. Strings are `u32 length` + bytes.

## Layout

| Field | Notes |
|---|---|
| `u32 version` | always 3 |
| `u32`, `u32` | always 0, 8 |
| `f32 bbox[6]` | min xyz, max xyz |
| 24 bytes | always 0 |
| 3 material groups | each `u32 n`, then `n × { u32 first_index, u32 count, string texture }` |
| `u32` | always 0 |
| collision block | optional, starts with `fa c2 97 eb` (same as `.sm` collision data), **no size field**; 78 of 111 base files |
| `u32 nverts`, `nverts × 44 bytes` | vertices |
| `u32 nindices`, `nindices × u16` | indices; the file ends here |

Because the collision block has no size, readers locate the geometry from the end of the file: `nindices` is known from the groups, and the vertex count is the first `n` whose `u32` sits `44 n + 4` bytes before the index count.

## Groups

| Group | `count` unit | Use |
|---|---|---|
| 1 | 24 indices | geometry blocks (palm leaves) |
| 2 | 3 indices (triangle) | trunks, branches |
| 3 | 24 indices = 4 quads | camera-facing sprites (conifer branches) |

`nindices = 24·Σcount₁ + 3·Σcount₂ + 24·Σcount₃` — holds for every retail file.

## Vertex (44 bytes)

`f32 pos[3]`, `f32 normal[3]`, `u32 colour` (`0x80808080` on geometry, 0 on sprites), `f32 uv[2]`, `f32 corner[2]`.

Sprite vertices (group 3) share their quad's centre in `pos`, have a zero normal, and carry the corner offset in `corner` (e.g. ±1.179). Geometry vertices have `corner = 0`. The viewer draws sprites as upright (cylindrical) billboards; the sign convention of `corner.y` was chosen visually.
