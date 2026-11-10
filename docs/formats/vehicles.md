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

## Engine model (implemented, unverified)

`engine/physics.cpp` reads a vehicle tree into wheels + hull ([ADR 0004](../architecture/adr/0004-jolt-physics.md)). Every interpretation below is a working assumption; none is measured against the original yet.

| Part | Reading |
|---|---|
| Root `PlayerControlObject` | `mass` in kg; `drag`: force = drag · mass · v² / 150 (placeholder: Willys tops out near 70 km/h) |
| Hull | convex hull of the collision vertices of every mesh except running gear (bounding boxes only if no part has a collision block). Running gear = an `AnimatedBundle` / `RotationalBundle` / `Engine` with `Spring` children, and everything below it: tank track meshes reach the wheel bottoms and would scrape. Centre of mass lowered to wheel-centre height (arcade stability) |
| `Spring` | a ray-cast wheel at the spring's position; radius = half the wheel mesh height; `setStrength` / `setDamping` per unit of the mass each wheel carries (s⁻², s⁻¹), pushing along the ground normal. A Willys (25, 5) then rests with its wheels at the model position, at 0.8 Hz and damping ratio 0.5, which is why this reading was chosen. Springs with strength 0 (tank "dummy" rollers, grip `c_PGFEngineDummyGrip`) carry nothing |
| Under `Engine` | driven wheel; drive acceleration = torque · differential / 20 m/s² split over driven wheels (placeholder); gears, `setMaxSpeed` 55000 not used |
| `RotationalBundle` with `setInputToYaw c_PIYaw` | steering: ± `setMaxRotation` yaw at `setMaxSpeed` °/s |
| `Engine` `c_ETTank` with `setInputToYaw c_PIYaw` | tracked: turns the hull on the spot at yaw `setMaxSpeed` × 8 °/s (placeholder; 4 for tanks, 1 for Priest and Sexton); side grip ignores the commanded turn |
| Grip | friction coefficient 1 for every wheel, sideways slip cancelled within the friction circle, applied at 80 % of the centre-of-mass height so cornering rarely rolls a vehicle; idle wheels brake and hold |

All 122 / 142 / 172 `PlayerControlObject`s of bf1942 / XPack1 / XPack2 build and simulate without error, except passenger seats and `Destroyable_Bridge`, which have no geometry of their own. On flat ground every land vehicle with wheels (7 cars, 14 tracked) settles upright and drives; the M3A1 half-track and the Sexton still touch the ground with their hull and accelerate at about two thirds of the others.

## Open questions

- Units of `setTorque`, `setDifferential`, `drag`, the 55000 engine speed; gear changes (`setGearUp/Down`).
- `c_PGFRollGrip` vs `c_PGFEngineGrip`, combined with `materialFriction`.
- Aircraft (`Wing`, `LandingGear`), boats (`FloatingBundle`): not modelled.
- Track and wheel animation, turret and gun rotation (`RotationalBundle` on mouse input).
