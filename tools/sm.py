#!/usr/bin/env python3
"""Reader for Refractor static meshes (.sm, versions 9 and 10). Stdlib only.

Spec: docs/formats/sm.md

Usage: sm.py <BF1942_DIR> <mod> <mesh> <out_dir>
  Export LOD 0 of e.g. standardMesh/willys_body_m1 as OBJ + MTL + textures (opens in Blender).
"""
import os
import re
import struct
import sys

from con import load_mod

TRIANGLE_LIST, TRIANGLE_STRIP = 4, 5  # Direct3D primitive types


def parse(d):
    """-> {version, bbox, collision: [bytes], lods: [[{name, prim, fvf, stride, alpha, vertices, indices}]], end}"""
    version, _ = struct.unpack_from("<II", d, 0)
    if version not in (9, 10):
        raise ValueError(f"unsupported .sm version {version}")
    bbox = struct.unpack_from("<6f", d, 8)
    o = 0x20 + (1 if version == 10 else 0)  # v10 has one extra byte, meaning unknown
    (ncol,) = struct.unpack_from("<I", d, o)
    o += 4
    collision = []  # opaque blocks, layout not decoded yet
    for _ in range(ncol):
        (size,) = struct.unpack_from("<I", d, o)
        collision.append(d[o + 4:o + 4 + size])
        o += 4 + size
    (nlod,) = struct.unpack_from("<I", d, o)
    o += 4
    lods = []
    for _ in range(nlod):
        (nmat,) = struct.unpack_from("<I", d, o)
        o += 4
        mats = []
        for _ in range(nmat):
            (n,) = struct.unpack_from("<I", d, o)
            name = d[o + 4:o + 4 + n].decode("latin-1")
            o += 4 + n
            _, _, _, prim, fvf, stride, nv, ni, alpha = struct.unpack_from("<9I", d, o)
            o += 36
            if prim not in (TRIANGLE_LIST, TRIANGLE_STRIP) or stride not in (32, 40):
                raise ValueError(f"{name}: unsupported primitive {prim} / stride {stride}")
            mats.append({"name": name, "prim": prim, "fvf": fvf, "stride": stride, "alpha": alpha, "nv": nv, "ni": ni})
        for m in mats:
            # stride 32: pos, normal, uv; 40: + lightmap uv
            m["vertices"] = [struct.unpack_from(f"<{m['stride'] // 4}f", d, o + i * m["stride"]) for i in range(m["nv"])]
            o += m["nv"] * m["stride"]
            m["indices"] = list(struct.unpack_from(f"<{m['ni']}H", d, o))
            o += 2 * m["ni"]
            if (m["prim"] == TRIANGLE_LIST and m["ni"] % 3) or any(i >= m["nv"] for i in m["indices"]):
                raise ValueError(f"{m['name']}: bad index buffer")
            del m["nv"], m["ni"]
        lods.append(mats)
    if o > len(d):
        raise ValueError("truncated .sm")
    return {"version": version, "bbox": bbox, "collision": collision, "lods": lods, "end": o}


def triangles(m):
    idx = m["indices"]
    if m["prim"] == TRIANGLE_LIST:
        return [tuple(idx[i:i + 3]) for i in range(0, len(idx), 3)]
    tris = []
    for i in range(len(idx) - 2):  # strip: every other triangle flips winding
        a, b, c = idx[i:i + 3] if i % 2 == 0 else (idx[i + 1], idx[i], idx[i + 2])
        if a != b and b != c and a != c:  # skip degenerate joints
            tris.append((a, b, c))
    return tris


def to_obj(mesh, lod=0):
    out, base = [], 1
    for m in mesh["lods"][lod]:
        out.append(f"g {m['name']}\nusemtl {m['name']}")
        # Direct3D is left-handed, OBJ right-handed: negate z and reverse winding.
        out += [f"v {v[0]} {v[1]} {-v[2]}" for v in m["vertices"]]
        out += [f"vn {v[3]} {v[4]} {-v[5]}" for v in m["vertices"]]
        out += [f"vt {v[6]} {1 - v[7]}" for v in m["vertices"]]  # OBJ v axis points up, D3D down
        for tri in triangles(m):
            a, c, b = (base + j for j in tri)
            out.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
        base += len(m["vertices"])
    return "\n".join(out) + "\n"


def parse_rs(text):
    """.rs shader file -> {material name lower: {"shader": ..., key: value}}"""
    out = {}
    for name, shader, body in re.findall(r'subshader\s+"([^"]+)"\s+"([^"]+)"\s*\{(.*?)\}', text, re.S):
        props = {"shader": shader}
        for line in body.split(";"):
            k, _, v = line.strip().partition(" ")
            if k:
                props[k] = v.strip().strip('"')
        out[name.lower()] = props
    return out


def export(vfs, mesh_path, out_dir):
    base = mesh_path.lower().removesuffix(".sm")
    mesh = parse(vfs.read(base + ".sm"))
    shaders = parse_rs(vfs.read(base + ".rs").decode("latin-1")) if base + ".rs" in vfs.files else {}
    name = os.path.basename(base)
    os.makedirs(out_dir, exist_ok=True)
    mtl = []
    for m in mesh["lods"][0]:
        mtl.append(f"newmtl {m['name']}")
        tex = shaders.get(m["name"].lower(), {}).get("texture", "").lower()
        for ext in (".dds", ".tga"):
            if tex and tex + ext in vfs.files:
                fn = os.path.basename(tex) + ext
                with open(os.path.join(out_dir, fn), "wb") as f:
                    f.write(vfs.read(tex + ext))
                mtl.append(f"map_Kd {fn}")
                break
    with open(os.path.join(out_dir, name + ".mtl"), "w") as f:
        f.write("\n".join(mtl) + "\n")
    with open(os.path.join(out_dir, name + ".obj"), "w") as f:
        f.write(f"mtllib {name}.mtl\n" + to_obj(mesh))
    return mesh


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    vm = load_mod(sys.argv[1], sys.argv[2])
    mesh = export(vm.vfs, sys.argv[3], sys.argv[4])
    print(f"v{mesh['version']}, {len(mesh['lods'])} LODs, LOD0: {len(mesh['lods'][0])} materials -> {sys.argv[4]}")
