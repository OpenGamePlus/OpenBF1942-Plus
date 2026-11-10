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

All 122 / 142 / 172 `PlayerControlObject`s of bf1942 / XPack1 / XPack2 build and simulate without error, except passenger seats and `Destroyable_Bridge`, which have no geometry of their own. On flat ground every `setVehicleCategory VCLand` vehicle with wheels (7 cars, 13 tracked; Sherman and others declare their category elsewhere and were checked separately) settles upright and drives; the M3A1 half-track and the Sexton still touch the ground with their hull and accelerate at about two thirds of the others.

## Aircraft (implemented, unverified)

An aircraft is a `PlayerControlObject` whose `Engine` is `c_ETPlane`, plus `Wing` children: main wings with `setRegulateToLift`, ailerons (`setInputToPitch c_PIRoll`), elevators (`c_PIPitch`), a rudder and a fuselage side wing (both rolled −90°, so their normal is sideways). Example values (Mustang): main wings `setFlapLift 3`, `setRegulateToLift 4.91`, `setPositionOffset` = minus their position; ailerons `setWingLift 2.5`, `setFlapLift 2`, ±30° at 120 °/s.

| Part | Reading |
|---|---|
| `Wing` | force along the wing normal at position + `setPositionOffset` (vehicle axes; for main wings this is the model origin). Per wing, as acceleration × mass: −0.01 · `setWingLift` · v_n · \|v\| (angle-of-attack lift and damping, capped so it cannot overshoot in one step) − 0.001 · `setFlapLift` · deflection · v_f² (control). Constants are placeholders |
| Normal | wing local y with its forward part removed: sweep + dihedral tilt it ~0.5° back, which as built-in incidence made a hands-off Mustang loop slowly |
| Deflection | input × sign of `setAcceleration` (pitch component), moving at `setMaxSpeed` / range. A positive pitch rotation raises the trailing edge (Direct3D convention), so it pushes down. Result: `c_PIRoll` +1 rolls right, `c_PIYaw` +1 turns right (as cars), `c_PIPitch` +1 lowers the nose (stick forward) |
| `setRegulateToLift` | every retail regulated wing (27) says 4.91 = g/2. Read per wing, the Il-2 (one such wing) would get 0.5 g and the SBD (three) 1.5 g, so the regulated wings are taken to hold 1 g together, scaled by (v_f / 30 m/s)² below 30 m/s: aircraft fly level hands-off and climb by pointing the nose |
| `Engine` `c_ETPlane` | thrust along the body axis at the centre of mass: Σ `setTorque` / 4 m/s². Fighters have 15 × 5 (torque × differential), twins 2 × ~7.5 × ~2.1, the B-17 4 × 2.6 × 1.9: summed torque matches the fighters' (B-17 lower), the product does not. Wheels are not driven |
| Centre of mass | at the model origin, where the main wings' lift acts |
| `LandingGear` | its springs are wheels as on cars; no retraction |

Check against retail data: parked fighters are placed with pitch −11° to −14° (nose up); the Mustang rests at 14.4° on its springs alone.

Take-off sweep on flat ground (full throttle, pull up from 32 m/s): all single-engine aircraft, the Mosquito, Bf 110, B-17, Ho 229 and AW52 take off and climb. The C-47 has no regulated wing and does not lift off; the XPack2 jet and rocket aircraft (Goblin `setTorque` 100, Natter 500) are far too fast under this reading; the Wasserfall and the jetpack are not aircraft in this sense.

## Open questions

- Units of `setTorque`, `setDifferential`, `drag`, the 55000 engine speed; gear changes (`setGearUp/Down`).
- `c_PGFRollGrip` vs `c_PGFEngineGrip`, combined with `materialFriction`.
- Wings: `setPitchOffset`, `setWingToRegulatorRatio`, `rememberExcessInput`, `setNoPropellerEffectAtSpeed`, `inertiaModifier`; jets and rockets; landing gear retraction.
- Boats (`FloatingBundle`): not modelled.
- Track and wheel animation, turret and gun rotation (`RotationalBundle` on mouse input).
