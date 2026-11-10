#!/usr/bin/env python3
"""Reader for Refractor tree meshes (.tm, version 3). Stdlib only.

Spec: docs/formats/tm.md

Usage: tm.py <BF1942_DIR> <mod> <treeMesh/name>   print groups and counts
"""
import struct
import sys

from con import load_mod

VERTEX = struct.Struct("<3f3fI2f2f")  # pos, normal, colour, uv, sprite corner offset
INDICES_PER = (24, 3, 24)  # per count unit: group 1 blocks, group 2 triangles, group 3 sprite blocks


def parse(d):
    """-> {bbox, groups: [[(first_index, count, texture)] x 3], vertices: [tuple], indices: [int]}"""
    version, _, _ = struct.unpack_from("<3I", d, 0)
    if version != 3:
        raise ValueError(f"unsupported .tm version {version}")
    bbox = struct.unpack_from("<6f", d, 12)
    o = 0x3C
    groups = []
    for _ in range(3):
        (n,) = struct.unpack_from("<I", d, o)
        o += 4
        entries = []
        for _ in range(n):
            first, count, ln = struct.unpack_from("<3I", d, o)
            entries.append((first, count, d[o + 12:o + 12 + ln].decode("latin-1")))
            o += 12 + ln
        groups.append(entries)
    ni = sum(c * per for g, per in zip(groups, INDICES_PER) for _, c, _ in g)
    # Geometry is the tail of the file; the collision block before it has no size field, so anchor at the end.
    end = len(d)
    vend = end - 2 * ni - 4
    if vend < o or struct.unpack_from("<I", d, vend)[0] != ni:
        raise ValueError("index count does not match material groups")
    nv = next((n for n in range(1, (vend - o) // VERTEX.size + 1)
               if struct.unpack_from("<I", d, vend - VERTEX.size * n - 4)[0] == n), None)
    if nv is None:
        raise ValueError("vertex block not found")
    vs = vend - VERTEX.size * nv
    vertices = [VERTEX.unpack_from(d, vs + VERTEX.size * i) for i in range(nv)]
    indices = list(struct.unpack_from(f"<{ni}H", d, end - 2 * ni))
    if any(i >= nv for i in indices):
        raise ValueError("index out of range")
    return {"bbox": bbox, "groups": groups, "vertices": vertices, "indices": indices}


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    vm = load_mod(sys.argv[1], sys.argv[2])
    t = parse(vm.vfs.read(sys.argv[3].lower().removesuffix(".tm") + ".tm"))
    for i, g in enumerate(t["groups"], 1):
        for first, count, tex in g:
            print(f"group {i}: indices {first}+{count * INDICES_PER[i - 1]} texture {tex}")
    print(f"{len(t['vertices'])} vertices, {len(t['indices'])} indices")
