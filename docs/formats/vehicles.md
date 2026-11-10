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

## Boats (implemented, unverified)

A boat has `FloatingBundle` children (4 on the LCVP, 6 on the Elco, 8 on destroyers and carriers), water rudders (vertical `Wing`s on `c_PIYaw`, one at each end with opposite `setAcceleration`), and a `c_ETShip` engine. Amphibians (LVT-4, Schwimmwagen, Elco beaching wheels) also carry wheels and a car or tank engine.

| Part | Reading |
|---|---|
| `FloatingBundle` | a buoyant column from its position down `setHullHeight`; lifts `setFloatMaxLift` × mass × the fraction of the column under `waterLevel`, straight up, and damps vertical motion. `setFloatMinLift`, `setSinkingSpeedMod`, `setDragModifier` unused |
| Water drag | sideways slip of the hull damped at the centre of mass (a keel, 2 /s × wet fraction); damping at each float would also brake rotation and pin long hulls (a Fletcher turned 0.08 °/s). Forward drag is the template's `drag` |
| `Engine` `c_ETShip` | propeller: Σ `setTorque` / 4 m/s² while any float is wet; half power astern. Torque × differential would make the Elco PT boat as slow as an LCVP; torque alone ranks them Elco 38, Type 38 27, LCVP 20 m/s, destroyers and carriers ~5 m/s |
| Rudders | the aircraft wing model unchanged (flap lift × speed²) |

Check against retail data: with the float reading every boat rests where the levels place it (origin − `waterLevel`):

| Boat | Simulated rest | Retail spawn heights |
|---|---|---|
| LCVP | −1.44 | −1.44 |
| Hatsuzuki | −5.62 | −5.64, −5.60, −5.45 |
| Prince of Wales | −7.42 | −7.35, −7.26 |
| Fletcher | 0.26 | 0.33 … 0.47 |
| Shokaku | 0.33 | 0.33 |
| Sub 7C | −0.94 | −0.83, −1.08 |
| Elco / Type 38 | −1.82 | −1.45 … −2.78 |

Yamato (−2.54) and Gato (−2.62) share spawners with other ships, so their retail heights are ambiguous.

Sweep in 30 m of water: every `VCSea` vehicle with floats and both amphibians float upright without drift, reach their top speed and turn on the rudder: boats 12–20 °/s, submarines 2–3 °/s, destroyers 0.7–1.4 °/s, carriers and battleships 0.2–0.3 °/s.

## Moving parts (drawing)

`build_object` takes a joint function: an extra local transform per template node, keyed by lower-case template name and visit index of that name in pre-order (the physics walker counts the same way; all 2,783 joints of the three mods are found). Applied after `setPosition` / `setRotation`, so children follow.

| Part | Motion |
|---|---|
| `RotationalBundle` | per axis (yaw / pitch / roll = `setInputToYaw/Pitch/Roll`): absolute inputs (`c_PIYaw`, `c_PIPitch`, `c_PIRoll`, `c_PIThrottle`) drive towards `setMinRotation` / `setMaxRotation` at `setMaxSpeed` °/s, back to 0 with `setAutomaticReset`; `c_PIMouseLookX/Y` turn at `setMaxSpeed` and stop at the limits if any (turret yaw has none). Direction: sign of `setAcceleration`. Steering hubs, steering wheels, turrets, guns, the LCVP ramp |
| `Spring` | rolls with the ground speed (dummy rollers with the hull's) and sits where its spring is compressed |
| `Wing` | pitched by its deflection × range |
| Meshes directly under a `c_ETPlane` engine | propeller spin, 300 + 1,500 × throttle °/s (guessed) |

Not moved: `Engine` rotation (a car's wheels hang under it), `AnimatedBundle` (tracks, hand IK), `LandingGear` retraction, ships' `FloatingBundle` pitch input.

## Soldiers on foot (implemented, unverified)

`--play 1|2` plays a team (soldier and kit from the level, see [weapons.md](weapons.md#teams-kits-and-soldiers)). `BFSoldier` templates (e.g. `USSoldier`: `mass 100`, `speedmod 0.5`, `setAnimationState 0 Lb_Stand`) carry no walking speeds. The engine uses a 1.8 × 0.3 m capsule (Jolt `CharacterVirtual`): runs at 5 m/s, walks at 2.5 m/s, jumps at 4.5 m/s, climbs 0.4 m steps and 50° slopes, and swims with the feet 1.4 m under the surface; all guessed. The lower-body state follows the movement (`Lb_Stand`, `Lb_RunForward/Backward`, `Lb_StrafeLeft/Right`).

Postures (left ctrl held crouches, Z toggles prone):
- The capsule shrinks to 1.2 m crouching and 0.7 m prone. It stays upright when prone, not lying along the body.
- Speed drops to half crouching and a quarter prone. Jumping only works standing; all of these are guessed.
- Standing up fails under anything lower than a standing soldier.
- Animations: `Lb_Crouch*` / `Ub_Crouch` and `Lb_Lie*` / `Ub_Lie` (from `animationStatesCrouching.con` and `animationStatesLie.con`). The transitions (`Lb_StandToCrouch`, …) are not played.
- Hits on a crouching or prone player are mapped to head, chest or limbs as if standing.

Getting in: the nearest vehicle within 5 m + half its length (not yet the `EntryPoint` radius). Getting out: at the vehicle's `setSoldierExitLocation` (Willys: −1.5/0/−0.8, left rear).

## Parked vehicles

With `--play` or `--drive` every `ObjectSpawner` placement that can move (wheels, wings or floats; stationary guns stay static) becomes a physics vehicle at its retail position, team as in the viewer. `build_object` does not follow spawners inside a vehicle (a destroyer's landing craft are vehicles of their own).

| Fix | Why |
|---|---|
| Lift until no hull point or wheel bottom is under the terrain | some retail spawns (M3A1, T95, Sexton, Willys) sit slightly under our terrain and fell through it |
| One convex hull per collision part, parts over 20 m as 8 m columns (triangles sampled every metre, 1 m overlap) | a carrier's deck, island and mast are one mesh; its single hull covered the deck and threw parked aircraft off |
| Aircraft brake gently above 0.5 m/s | the hard idle brake nosed tail-draggers over |

Settling sweep, all 78 levels, 1,546 parked vehicles, 5 s: none tipped; 103 only settled vertically (boats to their waterline, aircraft riding their carrier); 16 moved more than 2 m sideways (Aichi Vals ~5 m, Natters on their ramps, Elcos at a pier); 20 fell more than 10 m: LCVPs placed on transports' davits drop into the sea, and two Salerno Willys (unexplained).

## Open questions


## Open questions

- Units of `setTorque`, `setDifferential`, `drag`, the 55000 engine speed; gear changes (`setGearUp/Down`).
- `c_PGFRollGrip` vs `c_PGFEngineGrip`, combined with `materialFriction`.
- Wings: `setPitchOffset`, `setWingToRegulatorRatio`, `rememberExcessInput`, `setNoPropellerEffectAtSpeed`, `inertiaModifier`; jets and rockets; landing gear retraction.
- Boats: big ships turn very slowly (0.2–1.4 °/s; the original is unmeasured; inertia from the full hull?); submarine diving (`FloatingBundle` with `setInputToPitch`); the unused float settings.
- Track and wheel animation, turret and gun rotation (`RotationalBundle` on mouse input).
