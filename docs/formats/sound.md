# Sound: `.wav` and `.ssc` scripts

Status: **`.wav` verified (every file in the three mods is RIFF PCM 16-bit, mono or stereo); `.ssc` keywords surveyed and the common ones used, their exact semantics unverified (inferred from names and values).** Code: `engine/audio.*`, `engine/sound.*`; decision: [ADR 0006](../architecture/adr/0006-audio.md).

## Files

- `sound/11khz|22khz|44khz/<name>.wav`: every sound at three rates. Some ship in a level archive (`bf1942/levels/<map>/sound/...`), so they exist only once that level is loaded. Scripts load them as `@ROOT/Sound/@RTD/<name>.wav`; `@RTD` is the rate folder. We use `44khz`.
- Templates name a script with `ObjectTemplate.loadSoundScript <path>`, relative to the folder of the `.con` that created the template; `..` occurs. Types seen: `BFSoldier`, `EffectBundle`, `RotationalBundle`, `FireArms`, `Engine`, `Projectile`, `HandFireArms`, `Wing`, `LandingGear`, `SupplyDepot`, …

## Script syntax

Line-based text with CRLF line ends. `#` starts a comment line, `*** … ***` lines are labels, and `/* … */` blocks are commented out. Keywords are case-insensitive.

| Keyword | Meaning (as used) |
|---|---|
| `#templateLevel HIGH\|MEDIUM\|LOW` | lines that follow apply to that sound-quality level. Usually each level only `#include`s `High/x.ssc`, `Mid/x.ssc` or `Low/x.ssc`; some ship only `High/`, so we use HIGH |
| `#include <path>` | relative to the including file |
| `newPatch` | starts a patch. Hand weapons have several, by position: 0 single shot, 1 reload, 2 distant shot, 5 automatic fire (looping; `silence.wav` on single-shot weapons) |
| `load <wav>` | starts a layer; the lines up to the next `load` set it |
| `loop`, `stereo`, `dopplerOff` | loop the layer; play it unpositioned (first-person sounds); no Doppler |
| `volume v`, `minDistance d`, `priority p` | gain (above 1 occurs, e.g. 10 on first-person shots; we cap it at 1); full level up to d metres, then 1/distance; voice priority (unused) |
| `stop FinishSample` | a released loop finishes the pass under way |
| `trigger Volume` | the layer starts, from its first sample, when its time / control volume first rises above 0 (distance not counted). Reloads and the knife chain layers this way (151 of 1,083 scripts); playing them all from time 0 left short clips silent (reported by the maintainer) |
| `randomStartPitch a / b`, `relativePosition x/y/z`, `randomPlay`, `addGroup` | not used yet (`randomPlay`: all variants play at once) |
| `beginEffect … endEffect` | one curve: `controlDestination Volume\|Pitch`, `controlSource Distance\|Time\|TimeRelease\|Default` (Default: the object's control, e.g. engine load), `envelope Ramp\|Linear`, then `param` values |

Curves:
- `Ramp x0 x1 y0 dy` gives `y0 + dy · clamp((x − x0)/(x1 − x0), 0, 1)`. Ramps with six params also occur; we use the first four.
- `Linear a b` gives `a + b · x`.

The layer's gain and pitch are the product of all its curves. So a weapon's near, far and distant layers fade in and out by distance, and an engine's start, idle, load and stop layers by time, control and release.

Sweep, every template's script in the three mods:
- 433, 503 and 558 scripts;
- 2,995 to 3,741 layers;
- every `.wav` decodes; 9 are missing in xpack2 because they ship in the Liberation of Caen archive.

The 31 to 82 scripts that come out empty are missing files or fully commented out.

## Use in the engine

- **What plays:** every shot (player, vehicles, bots) plays patch 0 and the distant patch 2 of its `FireArms` script at the muzzle. A weapon whose patch 5 loops (BAR, Thompson) keeps that loop running while its shooter fires, stopping 0.25 s after the last shot. A reload plays patch 1. The first build merged all patches into one, so each shot left a loop running; the maintainer heard it. Every intact vehicle with a driver (the player or a bot) loops the own script of each of its `Engine`, `AnimatedBundle` (tracks), `RotationalBundle` (turrets) and `Wing` parts. Curve sources: `Default` is the engine load (speed / 30 m/s, aircraft / 80) or a turret's turning rate in degrees per second, measured from its joint (0.1–40 on the T34-85 tower); `Speed` the vehicle's speed in m/s (unit unverified); `Acceleration` its change per second. The loops stop when the driver's seat empties or the vehicle is wrecked.
- **In flight:** a live projectile whose template has its own script (tank shells' `Shellwhine`, rockets) loops it at the projectile's position.
- **Footsteps:** a soldier template loads the scripts of `SoldierSound.inc` (stop, walk, run, crouch and crawl moves, jumps, swimming, parachute, hits, voice; shared folder `objects/soldiers/common/sounds`). Their patches are grounds: 0 sand, 1 metal, 2 wood, 3 concrete, 4 grass, 5 gravel, 6 frozen, then fabric and harness. Every soldier on foot steps at the include's rates (run 0.36 s above 4 m/s, walk 0.66, crouch 0.5, crawl 0.6), one variant of the patch for the terrain material under it (shortcut: materials grouped by name, static objects count as the ground under them). A jump the physics takes plays `SoldierJump` patch 0; a touch-down faster than 3 m/s (guessed) plays `SoldierJumpLand` on the ground patch. Posture changes play patch 0 of `SoldierToCrouch`, `SoldierFromCrouch`, `SoldierToCrawl` or `SoldierStandUp`; those patches chain their layers by `trigger Volume`, so a soldier patch with a triggered layer plays whole, any other plays one variant (rule inferred from the scripts). A soldier opening its parachute plays `SoldierOpenParachute`; one given a magazine at a supply depot `SoldierRefillAmmo`. Voices: a hurt soldier says `SoldierHitDamage` at most once a second (`setHitDamageFrequency 1.0`; the random 0.2 left out), a dying one `SoldierKilled`. Their `@Language` folder is the template's `setRadioLanguage` (`sound/44khz/<language>/`; English, German, Japanese, Russian, UsEnglish); Canadian has no folder in bf1942 and falls back to English (shortcut).
- **Ambient:** placed map objects whose `AnimatedBundle`, `Bundle`, `AreaObject`, `SimpleObject` or `RotationalBundle` parts load a script loop it at their position (flags at control points, guard towers, coastlines). Supply depots are left out: their sound belongs to a refill.
- **Impacts and explosions:** where a shot ends, its effect plays every patch of that `EffectBundle`'s script once. The effect is the projectile's `endEffectTemplate` (grenades, mines, packs), else the MaterialManager's `setEffectTemplate` for the projectile's material against the target: body part on soldiers, armour face or vehicle material, else the terrain material map under the hit (shortcut: static objects count as the ground under them). This gives ricochets, blood, shell and bazooka explosions. 61 of the 67 effects the retail tables name have a sound script; `e_richoPHeavy` (aircraft bullets) has none and 4 names are not defined in retail, so those stay silent.
- **Device:** SDL, 44.1 kHz float stereo, 1,024-frame blocks (23 ms). The viewer logs a `timing` line every 5 s: frame times, voices, the audio callback's time and the longest gap between callbacks.
- **Under WSLg** (the maintainer's setup) the device stalls: after about a minute the gap between callbacks grows from ~2 blocks to seconds (0.2 → 3.7 s), whatever the block size (512 or 1,024) or rate (44.1 or 48 kHz), while our callback takes under 2 ms. A bare SDL program with no window feeding silence stalls the same way (34 ms gaps for 70 s, then 0.9 s growing to 4.9 s at 3 minutes): the cause is WSLg's PulseAudio / RDP audio path, not the engine. A native build should not have it (unverified). The mixer caps voices at 256, dropping the oldest one-shots, so a stall does not end in a pile-up (1,393 voices before the cap).
- **Threads:** the mixer locks only its voice list; scripts and `.wav` files load on the game thread outside that lock. Holding one lock over both (first build) blocked SDL's audio thread whenever a new effect loaded from the archives: choppy, delayed sound, reported by the maintainer and fixed.
- **How it plays:** the listener is the camera. `obf_mapview` plays through SDL; `obf_sim` and `--shot` runs are silent.
- **Missing:** radio and `SoldierVoice` callouts (no radio commands in the game yet), `SoldierCharge`, swimming, the falling wind and scream (`SoldierFallingHigh`, loops), `SoldierHealedByMedic` (no medic healing yet), landing gear (no gear movement simulated).
