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

Values: quaternion component = `v / 32767` (unit length, verified); position = `v / 2^prec` metres (verified: thigh offset 0.094 m matches the skeleton). A bone's local transform is `T(p) · R(q*)`, with the rotation matrix of the **conjugate** quaternion (−x, −y, −z, w) for column vectors. We checked this in the data, not against the original: with the root rule below, the legs line up with the torso (3° instead of 11°), feet to head is 1.52 m (1.37 m read straight), knees bend forward in all 26 samples of the run cycle, and the left hand lies on the axis of the held weapon (see below). Read straight, soldiers leaned back about 28° and held weapons crossways.

Bones not driven by any active animation keep the skeleton's local matrix. Soldiers combine a lower-body animation (`Bip01`, pelvis, legs, `Spine Root`) with an upper-body one (spine, arms, head).

## Animation states

`animations/animationstates.con` (and the files it runs) define `AnimationStateMachine.createState <name>` followed by `addAnimation <baf> <speed> <flags>`, transitions and idles. Templates select states with `setAnimationState [index] <state>`: flags use `FlagBlow`; soldiers use index 0 (lower body, `Lb_Stand`) and 1 (upper body, `Ub_StandAim`, completed with the weapon name, e.g. `Ub_StandAimThompson`).

The viewer plays the first animation of each state, looping at 25 frames per second × speed — the frame rate is a guess. Transitions, idles and weapon selection are not implemented.

## Soldier root

Biped animations (`Bip01` root) are in 3ds Max space: up is −z, facing +y, the feet near z = −0.1 and the pelvis at z = −1.0. `pose()` keeps the animated root and turns everything a quarter turn about x, mapping (x, y, z) to (x, −z, y). The result is Y-up, facing +z, standing on the object origin, with no further turn when drawn.

## Held weapons (unverified)

Hand weapons have their own skeleton:
- The root is `Bip01 R Hand`. It is the hand's local transform from the soldier rig (translation 0.305 along the forearm) and is not used.
- A main bone (`Base`, `Thompson`, `Bar1918`; `useSkeletonPartAsMain`) is a child of the root, with parts under it (`trigger`, `mag`, `clip`; `bindToSkeletonPart` on the child template).
- The soldier skeleton has a `Thompson` bone under `Bip01 R Hand` with the same local transform as `thompson.ske`'s main bone.

We draw each part at:

`soldier hand × main bone local × R_y(180°) × locals below it`

Without the 180° turn the barrel points back through the body and the left hand sits on the barrel axis behind the grip. With it, the left hand is on the forestock (M1 Garand: butt 0.29 m behind the grip, left hand 0.27 m ahead, muzzle 0.82 m ahead). The turn is fitted, not explained.

Upper-body states are per weapon: `copyState2 <new> <from> [<3p> <speed> <1p> <speed>]` copies the state just created, replacing `<from>` in its name and animation paths (e.g. `Ub_StandAimThompson` → `Ub_StandAimBar1918`, with the `bar1918` animations when they exist). There are 1,217 calls.

## Fire and reload

Per weapon, from the Thompson states through `copyState2`:

| State | Plays | When |
|---|---|---|
| `Ub_Fire<w>` / `Ub_LieFire<w>` | looping (`c_AsmLooping`) | while shots come within 1.5 rounds' interval (at least 0.25 s; ours) |
| `Ub_StandReload<w>` / `Ub_LieReload<w>` | once (`c_AsmPlayOnce`) | from the start of a reload |
| `WeaponFire<w>`, `WeaponReload<w>` | on the weapon's own skeleton (bolt, magazine) | the same moments |

The upper body runs at its own speed and start time while the legs keep theirs; `pose()` takes one frame per animation. The state machine's own transitions (`addTransitionOne c_PIFire …`) are not interpreted.

## First person

- **Parts drawn:** the soldier template's `1p*` children (hands) and `*1PBody` (sleeves). The third-person body is hidden.
- **Animations:** each state's second `addAnimation` is its first-person one (`1PStandAimThompson.baf`). `copyState2`'s fifth argument names the weapon used for it.
- **Placement:** the rig hangs from the camera at the eye, offset by the soldier's `center1pHands` (−0.12/−1.56/0.1). It is posed as usual, so the weapon in its right hand points straight ahead (checked: gun axis (0, 0.05, 1)).
- **Drawing:** the rig and weapon are drawn after the scene, over a cleared depth buffer, with a 5 cm near plane. The field of view is `set1pFov` (0.47), read as vertical radians; that reading is unverified.
