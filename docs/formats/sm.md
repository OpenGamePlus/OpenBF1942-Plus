# SM static mesh format

Status: **verified** — `tools/sm.py` parses all 2,527 `.sm` files in bf1942, XPack1 and XPack2; every index is in range. Visually checked on a bunker and the Willys jeep.

Little-endian. Strings are `u32 length` + bytes, no terminator.

## Layout

| Field | Notes |
|---|---|
| `u32 version` | 9 or 10 |
| `u32` | always 0 |
| `f32 bbox[6]` | min xyz, max xyz |
| `u8` | **v10 only**; 0 or 1, meaning unknown |
| `u32 ncol` | collision meshes: 0, 1 or 2 |
| `ncol × { u32 size, size bytes }` | collision data, not decoded |
| `u32 nlod` | LOD count; trailing LODs can be empty (`nmat = 0`) |
| per LOD | `u32 nmat`, then `nmat` material headers, then the data of each material in header order |

Material header:

| Field | Values seen |
|---|---|
| name | `<mesh>_MaterialN`, matches a `subshader` in the `.rs` file |
| `3 × u32` | always 0 |
| `u32 primitive` | Direct3D primitive type: 4 = triangle list, 5 = triangle strip (first-person and face meshes) |
| `u32 fvf` | `0x411` (stride 32) or `0x2411` (stride 40) |
| `u32 stride` | 32: pos, normal, uv · 40: pos, normal, uv, lightmap uv (all `f32`) |
| `u32 nverts`, `u32 nindices` | |
| `u32 alpha` | 0, 1, 2, 4, rarely 9, 12; meaning unknown |

Material data: `nverts × stride` bytes, then `nindices × u16`.

After the last LOD: a footer, 62 bytes in most files, longer in 288 files where it seems to hold another mesh section. Not decoded.

## Coordinates

Direct3D convention, left-handed. The OBJ export negates z and reverses the winding.

## Shader file (`.rs`)

Text, next to the mesh (`standardMesh/x.sm` → `standardMesh/x.rs`):

```
subshader "<material name>" "StandardMesh/Default"
{
	lighting true;
	materialDiffuse 1 1 1;
	texture "texture/defgun_Stuff_s";
}
```

`texture` has no extension; the file is `.dds` or `.tga` in the VFS. Other keys seen: `lightingSpecular`, `materialSpecular`, `materialSpecularPower`, `envmap`, `twosided`, `transparent`, `textureFade`, `depthWrite`, `alphaTestRef`, `blendSrc`, `blendDest`.
