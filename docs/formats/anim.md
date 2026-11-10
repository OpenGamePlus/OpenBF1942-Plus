# Skeletons, skins and animations (`.ske`, `.skn`, `.baf`)

Status: **structure verified, pose semantics partly verified.** `tools/anim.py` parses all 86 `.ske`, 120 `.skn` and 1,593 `.baf` files of bf1942, XPack1 and XPack2 exactly to their end.

Little-endian. Names are `u16 length` + bytes including a trailing NUL.

## Skeleton (`.ske`)

| Field | Notes |
|---|---|
| `u32 version` | 1 |
| `u32 nbones` | |
| per bone | name, `i16 parent` (-1 = root; parents always come first), `f32[12]` row-major `[R \| t]` relative to the parent |

The stored pose is *not* the skin's bind pose (the flag skeleton holds a waving frame); skinning does not need a bind pose, see below.

## Skin (`.skn`)

| Field | Notes |
|---|---|
| `u32 version` | 1 |
| `u32 nrecords` | one per unique vertex position |
| per record | `f32 pos[3]` (mesh space), `u8 n`, `n × { u16 bone, f32 weight, f32 offset[3] }` |
| `u16 nbones`, names | bone table: `bone` indexes this list; names match skeleton bones |

- The `.sm` named by the `AnimatedMesh` geometry template (`file`) supplies vertices, UVs and materials; `setSkin` names the `.skn`.
- A mesh vertex belongs to the record with the same position. Exact float match for 60 of 89 animated meshes, within 1 mm for 4 more; the other 25 are tank tracks whose skin covers only part of the mesh (the rest stays rigid).
- Skinned position = `Σ weight × BoneWorld × offset` — the offset is already in bone space, so no inverse bind matrices.

## Animation (`.baf`)

| Field | Notes |
|---|---|
| `u32 version` | 3 |
| `u16 nbones`, names | bones this animation drives |
| `u32 nframes`, `u8 prec` | `prec` 11–15 |
| per bone | `u16 total_words`, then 7 channels: `qx qy qz qw px py pz` |
| per channel | `u16 words`, then blocks until `words` are used |
| block | `u8 head`, `u8` (unused); `n = head & 0x7f`; `head & 0x80`: one `i16` repeated `n` times, else `n` literal `i16` |

Every channel decodes to exactly `nframes` values; `total_words` = sum of the channel `words`.

Values: quaternion component = `v / 32767` (unit length, verified); position = `v / 2^prec` metres (verified: thigh offset 0.094 m matches the skeleton). A bone's local transform is `T(p) · R(q)` with the standard (x, y, z, w) quaternion matrix for column vectors — verified visually (knees bend forward in the run cycle; the conjugate gives impossible poses).

Bones not driven by any active animation keep the skeleton's local matrix. Soldiers combine a lower-body animation (`Bip01`, pelvis, legs, `Spine Root`) with an upper-body one (spine, arms, head).

## Soldier root (unverified)

The root `Bip01` carries 3ds Max space (translation `(0, 0, -1)`, ≈ -90° about z). With its transform replaced by identity the skeleton is Y-up and faces -z; the viewer then turns it 180° about y. The standing pose shows ≈ 20° hip flexion that could not be confirmed against the original game.
