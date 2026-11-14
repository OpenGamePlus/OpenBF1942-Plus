# Effects (`EffectBundle`, `Emitter`, `SpriteParticle`)

Status: **partly decoded, unverified.** Field meanings below are read from the data and fitted by eye; none is measured against the original client.

Effects are object templates in `objects/effects/<name>/effects.con` (202 folders in bf1942). They are loaded with the other object scripts ([con.md](con.md)).

## Templates

| Type | Count | Role |
|---|---|---|
| `EffectBundle` | 71 | an effect: `addTemplate` children (emitters), each placed by the `setPosition` / `setRotation` that follow it; `timeToLive` (`-1`: until stopped); `loadSoundScript` (its sound, [sound.md](sound.md)); `addWorkOnMaterial <n>` (only over terrain material `n`; `1` read as water) |
| `Emitter` | 111 | `template <particle>`; emits for `timeToLive` seconds at `intensity` particles per second, after `delay`; `looping 1` repeats |
| `SpriteParticle` | 84 | a camera-facing textured quad |
| `Particle` | 17 | a mesh particle (`geometry`, a `GeometryTemplate` whose `file` names `standardmesh/<file>.sm`): muzzle flashes, ricochet glints, debris |
| `SimpleObject` | 23 | geometry used by the above |

## Values

`CRD_<kind>/a/b/sym` is a random value: `CRD_NONE` is `a`; `CRD_UNIFORM` lies between `a` and `b` (e.g. `size CRD_UNIFORM/2/0.8/0`); `sym 1` gives either sign (`initRotation CRD_UNIFORM/0/180/1`). `CRD_NORMAL` and `CRD_EXPONENTIAL` (6 uses each) are read as uniform.

`<field>OverTime t/v|t/v|…` is a curve over the particle's life, `t` in percent, linear between keys; it multiplies the field. `colorRGBAOverTime t/r/g/b/a|…` gives the colour (0–255).

## Emitter fields we read

| Field | Reading |
|---|---|
| `relativePositionIn{Right,Up,Dof}` | start offset in the emitter's frame (x, y, z) |
| `positionalSpeedIn{Right,Up,Dof}` | start velocity in the emitter's frame |
| `addEmitterSpeed`, `emitterSpeedScale` | adds the carrier's velocity, scaled |
| `showInFirstPerson` / `showInThirdPerson` | the world shows emitters without `showInFirstPerson` or with both; the first-person view those with `showInFirstPerson` or neither |
| `IntensityAtSpeed` | full intensity from this carrier speed (m/s), proportionally less below |
| `moveToWaterSurface` | particles start at the water level |

Not read: `startRotation`, `rotationalSpeedIn*`, `hasOverDamage`, `startProbability`, `moveToWaterSurface`, `lodDistance`.

## Particle fields we read

Mesh particles take the emitter's axes when emitted, turned about its z by `initRotation` / `rotationSpeed`, scaled by `size` × `sizeOverTime` (1 when absent), tinted by `colorRGBAOverTime`; their `.rs` blending applies (`transparent true`, `blendDest one`: added without writing depth; the muzzle flashes), others are drawn opaque, unlit.


`timeToLive`, `size` (quad edge in metres), `sizeOverTime`, `gravityModifier` (× 9.81 m/s² downwards; negative rises) and its curve, `drag` (velocity loss per second), `initRotation` and `rotationSpeed` (degrees, degrees per second) and its curve, `XYSizeRatio`, `colorRGBAOverTime`, `alphaOverTime`, `texture` (`texture/<name>`), `destBlendMode` (`BMOne` adds; otherwise alpha blending), `numAnimationFrames` (a square grid of frames, row by row), `initAnimationFrame`, `animationSpeed` (frames per second).

Not read: `srcBlendMode`, `hasStaticColor`, `animationSpeedOverTime`, `dragOverTime`, `XYSizeRatioOverTime`, `sizeModifier`, `turnsInMovingDirection`, `setHasCollisionPhysics`.

## Where effects come from

| Source | Field | Shown |
|---|---|---|
| Hand and vehicle weapons | an `EffectBundle` child whose name has `muzz` (`e_MuzzGun`), placed by its `setPosition` | at the muzzle on each shot, facing the shot; in first person the renderer shows the 1p emitters at that point of the 1p weapon (on its main bone, turned as the weapon) and draws them with the arms |
| Projectiles | `endEffectTemplate` | where the shot ends |
| Hits without one | the MaterialManager's effect for the pair ([weapons.md](weapons.md)) | ricochets, blood, sand |
| Vehicle parts | an `EffectBundle` child anywhere in the vehicle tree, outside its weapons (`e_wdustPanz` on track wheels, `e_ExFumePanz`, `e_WaterFrontBig`) | while the vehicle has a driver and is not wrecked, at the part's rest position, over its listed materials and within 1.5 m of that ground or the water |
| Vehicles | `addArmorEffect <hp> <effect> <x/y/z>` | `<hp>` > 0: while hit points are below it, on the body at that offset (damage smoke, then fire); `0`: once on destruction; `-1`: in water (not shown) |

Impacts face up (hits do not carry the surface normal). A wreck's damage effects run on for 30 s (guessed).

## Use in the engine

`engine/effects.*` (game library, no GL) parses the templates on first use and simulates particles on the CPU; `Game` spawns them and the renderer draws `Game::effect_sprites()` after the world and before the first-person weapon, farthest first, depth-tested without writing depth. A cap of 400 particles per emitter (guessed). An emitter's first particle leaves at once (a flash cannot wait for its rate), the rest at `intensity` per second. Tested on a synthetic bundle in `test_engine`.
