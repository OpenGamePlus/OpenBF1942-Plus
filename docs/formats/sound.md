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
| `randomStartPitch a / b`, `relativePosition x/y/z`, `trigger`, `randomPlay`, `addGroup` | not used yet (`randomPlay`: all variants play at once) |
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

- **What plays:** every shot (player, vehicles, bots) plays patch 0 and the distant patch 2 of its `FireArms` script at the muzzle. A weapon whose patch 5 loops (BAR, Thompson) keeps that loop running while its shooter fires, stopping 0.25 s after the last shot. A reload plays patch 1. The first build merged all patches into one, so each shot left a loop running; the maintainer heard it. Every intact vehicle with a driver (the player or a bot) loops its `Engine` script, with control = speed / 30 m/s (aircraft / 80); the loop stops when the driver's seat empties or the vehicle is wrecked.
- **Impacts and explosions:** where a shot ends, its effect plays every patch of that `EffectBundle`'s script once. The effect is the projectile's `endEffectTemplate` (grenades, mines, packs), else the MaterialManager's `setEffectTemplate` for the projectile's material against the target: body part on soldiers, armour face or vehicle material, else the terrain material map under the hit (shortcut: static objects count as the ground under them). This gives ricochets, blood, shell and bazooka explosions. 61 of the 67 effects the retail tables name have a sound script; `e_richoPHeavy` (aircraft bullets) has none and 4 names are not defined in retail, so those stay silent.
- **Threads:** the mixer locks only its voice list; scripts and `.wav` files load on the game thread outside that lock. Holding one lock over both (first build) blocked SDL's audio thread whenever a new effect loaded from the archives: choppy, delayed sound, reported by the maintainer and fixed.
- **How it plays:** the listener is the camera. `obf_mapview` plays through SDL; `obf_sim` and `--shot` runs are silent.
- **Missing:** soldiers (steps, voices), projectiles in flight (`Projectile` scripts, shell whistle), aircraft wings and landing gear, turret and track movement, ambient bundles, radio.
