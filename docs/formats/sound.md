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
| `newPatch` | starts the patch (one per file in practice) |
| `load <wav>` | starts a layer; the lines up to the next `load` set it |
| `loop`, `stereo`, `dopplerOff` | loop the layer; play it unpositioned (first-person sounds); no Doppler |
| `volume v`, `minDistance d`, `priority p` | gain (above 1 occurs, e.g. 10 on first-person shots; we cap it at 1); full level up to d metres, then 1/distance; voice priority (unused) |
| `randomStartPitch a / b`, `relativePosition x/y/z`, `trigger`, `stop`, `randomPlay`, `addGroup` | not used yet |
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

- **What plays:** every shot (player, vehicles, bots) plays its `FireArms` script at the muzzle. The vehicle under control loops its `Engine` script, with control = speed / 30 m/s (aircraft / 80).
- **How it plays:** the listener is the camera. `obf_mapview` plays through SDL; `obf_sim` and `--shot` runs are silent.
- **Missing:** soldiers, projectiles in flight, impacts, explosions, ambient bundles, radio.
