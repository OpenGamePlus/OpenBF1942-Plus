# Weapons and damage

Status: **damage formula checked against the retail tables' own comments and known behaviour; projectile flight unverified.** Implementation: `engine/damage.*`, `engine/physics.cpp` (projectiles, armour), `engine/mapview.cpp` (firing, hit points, wrecks).

## Weapons

`FireArms` (vehicle guns) and `HandFireArms` (soldier weapons) name a `Projectile` template:

| Property | Meaning | Examples |
|---|---|---|
| `projectileTemplate` | the projectile | `ShermanProjectile` |
| `velocity` | muzzle speed, m/s | Sherman 100, MG42 1000, M1 Garand 1000, Mustang guns 400 |
| `roundOfFire` | rounds per second | Sherman 0.35 (one shell every 2.9 s), M1 Garand 3.2, Thompson 10, MG42 15 |
| `magSize`, `numOfMag`, `reloadTime` | rounds per magazine, magazines (read as including the loaded one; unverified), reload seconds | M1 Garand 8, 4, 4.0 |
| `fireOnce` | one round per trigger pull | M1 Garand 1 |
| `ammoType` | which supply depots refill it | rifles 1 |

Projectile `radius` (29 projectiles, all explosive: grenades 15, bazooka 4, shells 10–20, bombs 20–30, depth charge 100): the explosion. We apply the projectile's `material2` damage to every soldier and vehicle within it, scaled linearly from full at the centre to 0 at the edge (unverified), and no longer add it to the direct hit. Shots with a radius whose `timeToLive` runs out explode where they are (grenade fuses).

Projectiles with `dieAfterColl 0` bounce instead of hitting and come to rest: grenades (3 s fuse), landmines (360 s) and explosive packs (240 s). We use a restitution of 0.3 and keep 60 % of the sliding speed (guessed). Mines go off when any vehicle comes within `explodeNearEnemyDistance` (3 m) of it; friendly vehicles set them off too. The player's explosive packs go off with G; the original uses the alternate fire. `minDamage`, `distToStartLoseDamage`, `distToMinDamage` and `forceOnExplosion` are not read yet.

Kits: the player carries every kit weapon that fires (e.g. Allied assault: BAR 1918, Colt, knife, grenades), keys 1–9, each with its own magazines.

Supply depots (`SupplyDepot`, under placed objects such as `Ammobox`, `mediclocker`, the airfield ammo; 1,567 over the 78 levels): `radius`, `team` (0 both), `workOnSoldiers` / `workOnVehicles`, `addAmmoType <type> -1 <rate> 0` (Ammobox: type 1 at 15), `setHealth -1 <rate> 0` (medic locker 4.0). We read the rates as rounds and hit points per second (unverified). Soldiers in range get spare magazines and health; bots out of spare magazines walk to the nearest depot of their team that stocks their ammunition and wait there until full.

Medic pack (`MedPack`, a `HandFireArms` with `magType 1`, "healing type"; `magSize 1800`, `numOfMag 1`, `roundOfFire 10`, no projectile): each round heals the first hurt teammate within the soldier's `healDistance` (10 m, 3D, CommonSoldierData.inc) by `healFactor` (0.25), else the medic itself by `selfHealFactor` (0.15) (Mac client `BFSoldier::useMedPack`, applied as negative damage; hit points per round inferred). A 30 hit-point soldier heals itself 10 → 24 in 10 s. With nobody hurt the pack is not used. Kits list it among their weapons; bots keep their first weapon with a projectile.

`Projectile`: `material` (hit), `material2` (explosion; the damage file calls it splash damage), `timeToLive CRD_NONE/<s>/…`, `damageType`.

The engine fires a vehicle's first `FireArms` with a projectile from that part's posed transform (turret and gun pitch included), along its local +z. Projectiles fly with full gravity and no drag (unverified).

## Damage

`MaterialManager` commands, loaded from `bf1942/game/materialmanagersettings.con` (it runs `materialmanagerdefine.con` and `damage_system/*.con`; the engine runs it, assumed like `animationstates.con`):

- `material <n>` with `materialAttGroup`, `materialDefGroup`, `materialDamage`, `materialFriction`, … (158 materials).
- `attGroup <a>` / `defGroup <d>` / `damageMod <m>` (5,147 pairs). `setEffectTemplate <effect>` after a pair names the `EffectBundle` played where that attack meets that defence (4,977 lines in `game/`: ricochets, blood, explosions); used for impact sounds.

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

## Conquest (implemented, rules unverified)

`engine/conquest.*`. Flags are the level's `ControlPoint` instances: `radius`, `team` (0 nobody), `timeToGetControl`, `timeToLoseControl`, `unableToChangeTeam` (main bases), `spawnGroupId`, `setControlPointName`. Tickets: `game.setNumberOfTickets <team> <n>`, `game.setTicketLostPerMin <team> <n>` (El Alamein: 100 each, 5 a minute).

Rules as read from those fields (the retail rules are not documented): soldiers of one team alone inside a flag's radius first neutralise an enemy flag (`timeToLoseControl`), then take a neutral one (`timeToGetControl`); the owner alone undoes progress; both teams present freeze it. A team holding fewer flags loses its `setTicketLostPerMin`; every death costs one ticket; a team at 0 loses. Spawn points belong to the team holding their flag, so captured outposts move the front.

## AI (implemented, placeholder)

Bots of both teams start on their team's spawn points (the player takes one place). Every 0.3 s a bot picks the nearest opponent (bot or player, on foot or in a vehicle) it can see within 120 m, checking the line of sight to the three nearest; with one it turns, waits 0.7 s and fires its kit weapon (the five classes in turn) at half the weapon's rate with ~2° of aim error. Otherwise it runs at 4 m/s for the flag it already chose (else the nearest its team does not hold), along a route on the level's infantry path-finding map ([pathfinding.md](pathfinding.md)) planned when the goal changes and again when stuck (at most every 5 s); still stuck, it sidesteps and jumps for 1.5 s. In a fight scouts lie down and the other classes crouch; they stand again to move. Bots whose kit has a hand grenade throw one at soldiers 12–35 m away, at most every 8 s, on the low arc that lands on the target. No damage from one's own team. Bots come back 10 s after dying, the player after 3 s, on a spawn point their team owns. No vehicles, no squads, no multi-storey routes; all numbers guessed.

Crews:
- **Boarding:** a bot with nothing to shoot boards a land vehicle within 10 m, checked every 3 s. It takes the driver's seat of an empty vehicle, or a free armed seat of one its team already crews.
- **Driving:** the driver heads for the nearest flag its team does not hold, along the level's tank path-finding map at 4 m cells, and stops 15 m short. It holds still while there is a target.
- **Stuck drivers:** a driver that is stuck (under 2 m in 3 s) backs up for 3 s. After four stuck checks in a row it gets out and does not board again for a minute.
- **Guns:** every armed seat turns its gun with the seat's mouse-look inputs, leading the shell's drop, and fires once within 3°.
- **Hits:** shots at the crew hit the vehicle, and a wreck kills its crew.
- **Landing craft:** troop boats (ships shorter than 16 m: LCVP, Higgins, Daihatsu) take passengers. The driver follows the landing-craft map (else the boat map; only 4 levels have either) to the flag. Everyone gets out once it is 40 m from the flag or aground.
- **Ship spawns:** a spawner with `teamOnVehicle 1` gives its team the spawn points inside the template it places (`FletcherSoldierSpawn`), and they move with the ship. The Allies on Omaha Beach, Wake and Iwo Jima now start on their ships, with 1,936 bots over the 78 levels instead of 1,689. Only Battle of Britain, Santo Croce and Essen still leave a team empty.
- **Not done:** aircraft, large ships, `setAIEnterOnSpawn`.

Over 78 levels in 5 minutes there were 718 boardings and 15 vehicles destroyed. Flag captures rose from 57 to 78 and bot deaths fell from 368 to 173, since bots inside vehicles are nearly immune to rifles. With ship spawns and landing craft: 982 boardings, 101 captures, 285 bot deaths.

El Alamein, simulated with `--sim`: outposts change hands from about 3 minutes in, flags go back and forth, and one team runs out of tickets after about 32 minutes. All 78 levels run a 2-minute game.

## Open questions

- Explosion radius (`material2` applies to the target hit only), effects (`e_*` templates), sounds.
- Magazines and reloading; projectile drag; `fireingForce`, `recoil*`.
- Secondary weapons and switching; held weapon drawing; soldiers spawning on vehicles (carriers); real skeleton hit boxes.
- AI: the level `ai/` strategies, vehicle use, cities (spawns inside buildings, narrow doorways); spawning on ships.
- Conquest: the real capture and ticket rules; other modes (CTF, TDM, co-op).
