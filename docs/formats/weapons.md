# Weapons and damage

Status: **damage formula checked against the retail tables' own comments and known behaviour; projectile flight unverified.** Implementation: `engine/damage.*`, `engine/physics.cpp` (projectiles, armour), `engine/mapview.cpp` (firing, hit points, wrecks).

## Weapons

`FireArms` (vehicle guns) and `HandFireArms` (soldier weapons) name a `Projectile` template:

| Property | Meaning | Examples |
|---|---|---|
| `projectileTemplate` | the projectile | `ShermanProjectile` |
| `velocity` | muzzle speed, m/s | Sherman 100, MG42 1000, M1 Garand 1000, Mustang guns 400 |
| `roundOfFire` | rounds per second | Sherman 0.35 (one shell every 2.9 s), M1 Garand 3.2, Thompson 10, MG42 15 |
| `magSize`, `numOfMag`, `reloadTime` | magazine | not used yet |

`Projectile`: `material` (hit), `material2` (explosion; the damage file calls it splash damage), `timeToLive CRD_NONE/<s>/…`, `damageType`.

The engine fires a vehicle's first `FireArms` with a projectile from that part's posed transform (turret and gun pitch included), along its local +z. Projectiles fly with full gravity and no drag (unverified).

## Damage

`MaterialManager` commands, loaded from `bf1942/game/materialmanagersettings.con` (it runs `materialmanagerdefine.con` and `damage_system/*.con`; the engine runs it, assumed like `animationstates.con`):

- `material <n>` with `materialAttGroup`, `materialDefGroup`, `materialDamage`, `materialFriction`, … (158 materials).
- `attGroup <a>` / `defGroup <d>` / `damageMod <m>` (5,147 pairs).

Damage of one hit = `materialDamage(m) × damageMod(attGroup(m), defGroup(target))` for the projectile's `material` and again for its `material2`, added.

The target material is the **armour face** hit: collision meshes carry a material per face, and vehicle parts with two collision blocks keep their armour materials in the second (that block is used for hits; unverified which block the original uses). A Sherman shell (236 + 206, `materialDamage` 10 each) against:

| Face | Material | Damage | Shots to kill (100 hp; Tiger 125) |
|---|---|---|---|
| Sherman rear | 50 | 100 | 1 |
| Sherman side | 51 | 50 | 2 |
| Sherman front | 52 | 40 | 3 |
| M10 | 53 | 25 | 4 |
| Tiger front | 54 | 10 | 13 |

Where no face is found the `PlayerControlObject`'s `material` is used. An M1 Garand bullet (220) does 37.5 to a soldier (material 40, 30 hit points) and nothing to tank armour.

At 0 `hitpoints` a vehicle is destroyed: it shows the last child of its `LodObject` when that is a wreck (`WillyWreck`), cannot be driven, and its driver is put out.

## Teams, kits and soldiers

A level's `Init.con` names each team's soldier (`game.setTeamSkin <team> <BFSoldier>`, El Alamein: 1 `GermanDesertSoldier`, 2 `BritishSoldier`) and five kits per team (`game.setKit <team> <0 scout | 1 assault | 2 anti-tank | 3 medic | 4 engineer> <Kit>`). A `Kit` adds `KitPart`s (helmet, packs) and `HandFireArms`; the first one with a projectile is taken as the primary (`GB_Assault`: `Bar1918`, then `Colt`, knife, grenades).

Soldier spawn points: a `SpawnPoint`'s `setGroup` matches a `ControlPoint` template's `spawnGroupId`; that control point's `team` (0 = nobody yet) owns it. The engine stands one enemy soldier on each spawn point of the other team (911 on the 78 levels; none on the Pacific maps, whose soldiers start on carriers).

Soldiers are hit through a body that follows their capsule. Body part by hit height (shortcut for `setSkeletonCollisionBone`): head above 1.45 m → material 40, chest above 0.85 m → 41, legs → 42. A BAR bullet does 14.4 to a chest: three hits kill (30 hit points). Dead soldiers play `Lb/Ub_DieChestStand` once and stay down.

## Open questions

- Explosion radius (`material2` applies to the target hit only), effects (`e_*` templates), sounds.
- Magazines and reloading; projectile drag; `fireingForce`, `recoil*`.
- Secondary weapons and switching; held weapon drawing; soldiers spawning on vehicles (carriers); real skeleton hit boxes.
