#!/usr/bin/env python3
"""Skeletons (.ske), skins (.skn) and animations (.baf). Stdlib only.

Spec: docs/formats/anim.md

Usage: anim.py <BF1942_DIR> <mod> <file.ske|.skn|.baf>   print a summary
"""
import struct
import sys

from con import load_mod


def _names(d, o, count):
    out = []
    for _ in range(count):
        (n,) = struct.unpack_from("<H", d, o)
        out.append(d[o + 2:o + 2 + n].rstrip(b"\0").decode("latin-1"))
        o += 2 + n
    return out, o


def parse_ske(d):
    """-> [(name, parent index or -1, 12 floats: row-major [R | t] relative to the parent)]"""
    version, n = struct.unpack_from("<II", d, 0)
    if version != 1:
        raise ValueError(f"unsupported .ske version {version}")
    o, bones = 8, []
    for i in range(n):
        (name,), o = _names(d, o, 1)
        (parent,) = struct.unpack_from("<h", d, o)
        if parent >= i:
            raise ValueError("parent after child")
        bones.append((name, parent, struct.unpack_from("<12f", d, o + 2)))
        o += 50
    if o != len(d):
        raise ValueError("trailing bytes in .ske")
    return bones


def parse_skn(d):
    """-> (records [(pos, [(bone, weight, offset)])], bone names); bone indexes into the names."""
    version, n = struct.unpack_from("<II", d, 0)
    if version != 1:
        raise ValueError(f"unsupported .skn version {version}")
    o, recs = 8, []
    for _ in range(n):
        pos = struct.unpack_from("<3f", d, o)
        k = d[o + 12]
        o += 13
        inf = []
        for _ in range(k):
            (bone,) = struct.unpack_from("<H", d, o)
            (w,) = struct.unpack_from("<f", d, o + 2)
            inf.append((bone, w, struct.unpack_from("<3f", d, o + 6)))
            o += 18
        recs.append((pos, inf))
    (nb,) = struct.unpack_from("<H", d, o)
    names, o = _names(d, o + 2, nb)
    if o != len(d) or any(b >= nb for _, inf in recs for b, _, _ in inf):
        raise ValueError("bad .skn")
    return recs, names


def parse_baf(d):
    """-> (bone names, frame count, [per bone: 7 channels of frame values]); channels qx qy qz qw px py pz."""
    (version,) = struct.unpack_from("<I", d, 0)
    if version != 3:
        raise ValueError(f"unsupported .baf version {version}")
    (nb,) = struct.unpack_from("<H", d, 4)
    names, o = _names(d, 6, nb)
    (nframes,) = struct.unpack_from("<I", d, o)
    prec = d[o + 4]
    o += 5
    bones = []
    for _ in range(nb):
        (total,) = struct.unpack_from("<H", d, o)
        o += 2
        used, chans = 0, []
        while used < total:
            (ln,) = struct.unpack_from("<H", d, o)
            o += 2
            end, vals = o + 2 * ln, []
            while o < end:  # blocks: count in low 7 bits; high bit = one repeated value
                head = d[o]
                n = head & 0x7F
                o += 2
                if head & 0x80:
                    vals += [struct.unpack_from("<h", d, o)[0]] * n
                    o += 2
                else:
                    vals += struct.unpack_from(f"<{n}h", d, o)
                    o += 2 * n
            if o != end or len(vals) != nframes:
                raise ValueError("bad .baf channel")
            used += ln
            chans.append(vals)
        if len(chans) != 7:
            raise ValueError("bone without 7 channels")
        q = [[v / 32767 for v in c] for c in chans[:4]]
        p = [[v / (1 << prec) for v in c] for c in chans[4:]]
        bones.append(q + p)
    if o != len(d):
        raise ValueError("trailing bytes in .baf")
    return names, nframes, bones


def _mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def ske_local(m):
    return [list(m[0:4]), list(m[4:8]), list(m[8:12]), [0, 0, 0, 1]]


def quat_local(q, p):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), p[0]],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), p[1]],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), p[2]],
            [0, 0, 0, 1]]


def pose(skeleton, animations=(), frame=0, biped_root=False):
    """World matrix per bone name. Bones an animation covers use its frame; others keep the skeleton.

    biped_root: replace the root ("Bip01") with identity. Its animated transform is 3ds Max space;
    without it a soldier is Y-up facing -z. Chosen visually, unverified against the original engine.
    """
    local = {name: ske_local(m) for name, _, m in skeleton}
    for names, nframes, bones in animations:
        f = min(frame, nframes - 1)
        for name, ch in zip(names, bones):
            if name in local:
                local[name] = quat_local([c[f] for c in ch[:4]], [c[f] for c in ch[4:]])
    if biped_root:
        local[skeleton[0][0]] = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    world = {}
    for name, parent, _ in skeleton:
        world[name] = local[name] if parent < 0 else _mul(world[skeleton[parent][0]], local[name])
    return world


def skin(vertices, records, names, world):
    """Skinned position per mesh vertex; mesh vertices are matched to skin records by position."""
    by_pos = {tuple(p): inf for p, inf in records}
    out = []
    for v in vertices:
        inf = by_pos.get(tuple(v[:3]))
        if inf is None:  # tolerance pass; tank tracks are only partly skinned and stay rigid
            best = min(records, key=lambda r: sum((a - b) ** 2 for a, b in zip(r[0], v[:3])))
            inf = best[1] if sum((a - b) ** 2 for a, b in zip(best[0], v[:3])) < 1e-6 else None
        if inf is None or any(names[b] not in world for b, _, _ in inf):
            out.append(tuple(v[:3]))
            continue
        p = [0.0, 0.0, 0.0]
        for b, w, off in inf:
            m = world[names[b]]
            for i in range(3):
                p[i] += w * (m[i][0] * off[0] + m[i][1] * off[1] + m[i][2] * off[2] + m[i][3])
        out.append(tuple(p))
    return out


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    vm = load_mod(sys.argv[1], sys.argv[2])
    path = sys.argv[3].lower()
    data = vm.vfs.read(path)
    if path.endswith(".ske"):
        for name, parent, m in parse_ske(data):
            print(f"{name:24} parent {parent:3}  t {m[3]:.3f} {m[7]:.3f} {m[11]:.3f}")
    elif path.endswith(".skn"):
        recs, names = parse_skn(data)
        print(f"{len(recs)} records, bones: {', '.join(names)}")
    else:
        names, nframes, _ = parse_baf(data)
        print(f"{nframes} frames, bones: {', '.join(names)}")
