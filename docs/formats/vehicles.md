# Vehicle templates (research for roadmap step 6)

Status: **structure surveyed; physical meaning of the parameters unverified.** Counts are `ObjectTemplate` types after loading the bf1942 mod (`tools/con.py`).

A vehicle is a tree of templates joined by `addTemplate` + `setPosition`/`setRotation` (see [con.md](con.md#object-placement)). Spawners place the root.

## Example: Willys jeep

```
Willy [PlayerControlObject]   mass 2500, drag 1.5, hitpoints 50, material 45
└ lodWilly [LodObject]        lodSelector WillyLodSelector
  ├ WillyComplex [Bundle]                     ← detail level 0
  │ ├ lodWillyCockpit [LodObject] → hull mesh (external / 1P)
  │ ├ WillyCamera [Camera], WillySeat [SeatObject], WillyEntry [EntryPoint] ×2
  │ ├ WillyPassengerPCO [PlayerControlObject] (passenger seat, camera, entries)
  │ ├ WillyEngine [Engine] @ 0/0.35/-0.25     torque 10.5, differential 7, 5 gears, gearUp 0.95, gearDown 0.4,
  │ │                                          input c_PIThrottle on roll, maxSpeed/acceleration 0/0/55000
  │ │ ├ WillyFrontWheelR/L [RotationalBundle] @ ±0.6/0.11/1.0   steer ±30° on c_PIYaw, speed/accel 200
  │ │ │ └ WillyFrontSpringR/L [Spring] @ 0/-0.599/0              wheel mesh, grip c_PGFRollGrip, strength 25, damping 5
  │ │ └ WillyBackSpringR/L [Spring] @ ±0.6/-0.472/-1.21          grip c_PGFEngineGrip, strength 25, damping 5
  │ └ WillySteeringDummy [AnimatedBundle] (hand IK) → WillySteering [RotationalBundle] ±60° roll on c_PIYaw
  ├ WillySimple [SimpleObject]                ← detail level 1 (Willy_Hull_L1)
  └ WillyWreck [SimpleObject]                 ← wreck
```

Reading of the tree (unverified): driven wheels hang under the `Engine`; front wheels steer through a `RotationalBundle`; each wheel is a `Spring` with stiffness, damping and a grip model (`c_PGFRollGrip` free-rolling, `c_PGFEngineGrip` driven).

## Physics-related types

| Type | Count | Typical properties |
|---|---|---|
| `PlayerControlObject` | 122 | `mass`, `drag`, `hitpoints`, `material`, vehicle category/type, exit location, HUD icons |
| `Engine` | 54 | `setEngineType` (`c_ETCar`, …), `setTorque`, `setDifferential`, `setNumberOfGears`, `setGearUp/Down`, input mapping (`setInputToRoll c_PIThrottle`), min/max rotation, speed, acceleration |
| `RotationalBundle` | 197 | rotating joints: wheels, turrets, guns, steering; `setMinRotation`/`setMaxRotation` (degrees, yaw/pitch/roll), `setMaxSpeed`, `setAcceleration`, `setInputToYaw/Pitch/Roll`, `setAutomaticReset` |
| `Spring` | 166 | wheels and track rollers: `geometry`, `grip`, `setStrength`, `setDamping` |
| `Wing` | 144 | aircraft surfaces: `setWingLift`, `setFlapLift`, `setPositionOffset`, `setPitchOffset`, regulator settings, input mapping |
| `LandingGear` | 39 | aircraft gear |
| `FloatingBundle` | 20 | boats |
| `SeatObject`, `EntryPoint`, `Camera` | 62 / 69 / 90 | crew positions, entry radius, camera limits |
| `LodObject` | 176 | detail levels as children, chosen by a `lodSelector`; the viewer draws the first child |

All physical parts carry `hasMobilePhysics`, `hasCollisionPhysics`, `hasResponsePhysics` flags. Collision shapes come from the meshes' collision blocks ([sm.md](sm.md#collision-block)); surface behaviour from `MaterialManager` ids ([terrain.md](terrain.md#material-map-materialmapraw)).

## Open questions for step 6

- Units and meaning of `setTorque`, `setDifferential`, `setStrength`, `setDamping`, `drag`, the 55000 engine speed.
- How `c_PGF*` grip models translate into friction, combined with `materialFriction`.
- Whether to reproduce the original handling numerically or approximate it (design decision).
