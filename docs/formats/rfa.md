# RFA archive format

Status: **verified** — `tools/rfa.py` decodes all 31,254 files of the retail install (base, XPack1, XPack2, sound archives included), each to its declared size.

All integers are little-endian `u32`.

## Layout

| Offset | Field | Notes |
|---|---|---|
| 0 | `dir_offset` | absolute offset of the directory |
| 4 | `compressed` | 1 = LZO1X segments, 0 = raw |
| 8 | padding | opaque bytes up to the first file; ignored |
| `dir_offset` | `count` | number of entries |

Directory entry (repeated `count` times):

| Field | Notes |
|---|---|
| `name_len`, `name` | path relative to the mod root, `/`-separated, mixed case (e.g. `bf1942/levels/Wake/Init.con`) |
| `stored_size` | bytes on disk including the segment table |
| `size` | decompressed size |
| `offset` | absolute offset of the file data |
| `unk` | same value for every entry of an archive (`0x1321e0` in `ai.rfa`); meaning unknown |
| 2 × reserved | always 0 |

## Compressed file data

```
u32 nseg
nseg × { u32 csize, u32 usize, u32 rel_off }   rel_off: from end of this table
LZO1X-1 streams, concatenated output = file
```

LZO1X is the standard algorithm (end marker `0x11 0x00 0x00`); implemented in pure Python in `tools/rfa.py`.

## Archive naming

- Archives live in `Mods/<mod>/Archives/`, sub-folders mirror the mount path (`Archives/bf1942/levels/Wake.rfa`).
- Suffixed archives (`texture_001.rfa`, `Wake_003.rfa`) are patches. Assumed (unverified): they override files of the same path in the base archive.
