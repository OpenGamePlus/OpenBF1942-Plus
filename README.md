# OpenBF1942-Plus

<p align="center"><img src="docs/banner.svg" alt="OpenBF1942-Plus" width="100%"></p>

![Status: early prototype](https://img.shields.io/badge/status-early%20prototype-orange)
![License: AGPL-3.0](https://img.shields.io/badge/license-AGPL--3.0-blue)
![Platform: Linux](https://img.shields.io/badge/platform-Linux-lightgrey)
![C++17 | OpenGL 3.3](https://img.shields.io/badge/C%2B%2B17-OpenGL%203.3-lightgrey)
![Battlefield 1942](https://img.shields.io/badge/Battlefield-1942-8a7a52)

[Español](README.es.md) · **English** · [Português](README.pt.md) · [Français](README.fr.md) · [Deutsch](README.de.md) · [Русский](README.ru.md)

**Battlefield 1942 on a new engine.** OpenBF1942-Plus is a clean-room reimplementation of Refractor 1, the engine of Battlefield 1942, written from scratch: it reads the game's own files from your installation and runs them on modern systems. The goal is to play with and against the original game: original clients on our server, our client on original servers.

<table>
  <tr>
    <td><img src="docs/images/first_person.jpg" alt="El Alamein in first person with the BAR"></td>
    <td><img src="docs/images/third_person.jpg" alt="A British soldier in third person at El Alamein"></td>
  </tr>
  <tr>
    <td colspan="2"><img src="docs/images/berlin.jpg" alt="A street in Berlin with tank traps"></td>
  </tr>
</table>
<p align="center"><sub>El Alamein and Berlin, drawn by OpenBF1942-Plus from the retail game files.</sub></p>

## What works today

- **Every level of Battlefield 1942, Road to Rome and Secret Weapons of WWII loads and renders**: terrain, buildings, trees, water, sky, fog and lighting from each level's own settings.
- **A conquest round against bots** on any level: flags, tickets, kits, magazines and reloads, grenades, mines and explosive packs, supply depots, crouch and prone, parachutes, first and third person.
- **Vehicles**: cars, tanks, aircraft and boats you can drive, with seats for gunners and passengers; bots drive and gun land vehicles and landing craft.
- **Sound and HUD** from the game's sound scripts and fonts.
- **Compared with the original client** at the same spots on Berlin and El Alamein: buildings, haze, sky, vehicles and sand within about 15 %.
- **Network research**: an original client/server session recorded; the server query protocol documented.

> [!NOTE]
> **Early prototype.** Only small parts have been tried by a person, many rules are guesses and nothing is calibrated. There are no menus, no multiplayer yet, and Berlin's streets are still darker than in the original.

> [!IMPORTANT]
> **No game content is included — bring your own data.** This repository is source code only: no executables, archives, maps, models, textures or sounds. The engine reads them from a copy of Battlefield 1942 that **you legally own**, referred to here as `<BF1942_DIR>`.

## How to play

There are no ready-to-run builds yet: build the engine (see [Building](#building)) and point it at your Battlefield 1942 folder, the one with `Mods/`:

```sh
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --play 2      # conquest on team 2, assault kit
build/obf_mapview "<BF1942_DIR>" bf1942 Wake                     # free camera
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy  # a vehicle in front of the camera
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600 # headless: 10 minutes of conquest, printed
```

| Controls | |
|---|---|
| Move, look | WASD, mouse (captured; F10 frees it), shift walks, space jumps (falling: opens the parachute) |
| Weapons | left mouse fires, R reloads, 1–9 kit weapons, G detonates explosive packs |
| Posture, view | left ctrl crouches, Z lies down, V first / third person |
| Vehicles | E gets in / out, 1–8 change seat, WASD drives, mouse aims turrets |
| Radio | F1–F7 opens a menu, then F1–F8 sends a message |
| Aircraft | W / S throttle, arrows pitch and roll, A / D rudder |
| Free camera | right mouse looks, WASD / QE move, shift fast; C shows collision meshes; Esc quits |

## Roadmap

The order, with the reasons and exit checks, is in [docs/roadmap/roadmap.md](docs/roadmap/roadmap.md):

1. **Consolidate** (done): the game as a library, a renderer and thin executables; CI.
2. **Network protocol research** (one session recorded): decode the original client/server traffic, alongside the steps below.
3. **AI navigation**, **audio**, **HUD** (first parts done).
4. **Complete soldiers and weapons** (mostly done), **vehicle crews** (land and sea done, pilots open), **effects**.
5. **Fidelity**: measure the original and calibrate.
6. **Game modes**, **menus**.
7. **Network**: our own server and client, then play together with the original.
8. **Rendering**, **distribution** (Windows and Linux builds, widescreen, mods).

## For contributors

Every kind of help counts: trying it and reporting what is wrong, comparing against the original game, or code. The technical documentation, in English, starts at [docs/README.md](docs/README.md): file formats, architecture decisions and known debt. [AGENTS.md](AGENTS.md) is the entry point for AI coding agents.

## Building

C++17, CMake, SDL2, OpenGL 3.3 (libepoxy) and Jolt Physics, which CMake downloads on the first configure. On Debian or Ubuntu:

```sh
sudo apt install cmake g++ git libsdl2-dev libepoxy-dev
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
# -> build/obf_mapview (window), build/obf_sim (headless)
```

<details>
<summary><b>Python tools</b></summary>

Readers for the game's formats, Python 3 standard library only.

```sh
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # load a level, print a summary
python3 tools/sm.py "<BF1942_DIR>" bf1942 standardMesh/willy_hul_m1 out/jeep   # OBJ + textures
python3 -m unittest discover tools     # tests
python3 tools/check_docs.py            # documentation checks
```

</details>

<details>
<summary><b>What is where</b></summary>

| Path | Contents |
|---|---|
| `engine` | The C++ engine: archives, scripts, meshes, terrain, animation, physics, game rules, AI, audio, HUD, renderer, and the executables |
| `tools` | Python readers for the formats, the documentation checker and their tests |
| `docs/formats` | The file and network format specifications |
| `docs/architecture` | Layers, boundaries and the architecture decisions (ADRs) |
| `docs/roadmap` | The roadmap and the known debt |
| `docs/ai` | The memory layer for AI agents |

</details>

## Transparency

- **Who maintains this:** OpenGamePlus. I like old games, and I want to keep playing Battlefield 1942 with friends on today's systems, with an open engine the community can keep alive.
- **How the work is done:** I work with [Claude Code](https://claude.com/claude-code), Anthropic's coding assistant. The code and documentation, this README and its translations included, are written with its help. Commits made this way carry a `Co-Authored-By: Claude` line in their message, so they can be identified in the history with `git log`.
- **Clean room:** no code of the original engine is used or decompiled. The engine is written from the game's data files and from observing the original game run.
- **What it is not:** it is not an official project and is not affiliated with Electronic Arts or DICE. It contains no game files.

## Notice

This is an independent reimplementation of the Refractor 1 engine. It is not affiliated with, authorized by, sponsored by, or endorsed by Electronic Arts Inc., EA Digital Illusions CE (DICE), or any of their affiliates.

"Battlefield" and "Battlefield 1942" are trademarks of Electronic Arts Inc. They are used in this repository only for identification and interoperability, to describe what the engine runs. No claim is made to those marks.

This project does not provide or help obtain copyrighted game data. To use it with real game content, you must supply data files from a copy of Battlefield 1942 that you legally own.

If you are a rights holder and believe something here should not be distributed, please open an issue or contact the maintainer and it will be addressed promptly.

The screenshots show content of Battlefield 1942, property of Electronic Arts Inc., for identification only.

## License

[GNU Affero General Public License v3.0](LICENSE). Whoever distributes the engine, or runs a modified version as a network server for others, must offer its source under the same license. Third-party libraries keep theirs: SDL2 (zlib), libepoxy (MIT) and Jolt Physics (MIT).

## Credits

- [Jolt Physics](https://github.com/jrouwe/JoltPhysics), [SDL](https://www.libsdl.org/) and [libepoxy](https://github.com/anholt/libepoxy)
- Electronic Arts / DICE, for the game
