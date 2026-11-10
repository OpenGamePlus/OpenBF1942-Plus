# OpenBF1942-Plus

[Español](README.es.md)

Clean-room reimplementation of the Refractor 1 engine so Battlefield 1942 runs on modern systems.

**No game data is included or redistributed.** The engine reads assets from your own installation, referred to as `<BF1942_DIR>`.

## Status

| Step | State |
|---|---|
| 1. Archives (RFA) | done — list/extract, verified on the full retail install |
| 2. Scripts (`.con`) | done — interpreter; every level of the three mods loads |
| 3. Static meshes (`.sm`) | done — every mesh parses; OBJ export opens in Blender |
| 4. Terrain + map objects | done — every map renders with buildings, trees, detail textures, per-level fog and sky |
| 5. Skinned meshes + animations | mostly done — soldiers, held weapons, first-person arms, fire and reload animations |
| 6. Physics + vehicles | in progress, uncalibrated — cars, tanks, aircraft, boats; crews with seats (F1–F8), seated soldiers |
| 7. Gameplay | early prototype — a conquest round against bots runs on any level, but only small parts have been tried by a person, many rules are guesses and nothing is calibrated. Implemented: kits, magazines, grenades, explosions, supply depots, crouch / prone, parachutes; bots route on the retail path-finding maps, drive and gun land vehicles and landing craft; sound and HUD |
| 8. Networking | research — one original client/server session recorded, query protocol documented |

Compared side by side with the original client (Berlin, El Alamein): buildings, haze, sky, vehicles and sand within about 15 %; Berlin's streets are still darker than in the original.

## Tools

Python 3, standard library only.

```
python3 tools/rfa.py list    "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa"
python3 tools/rfa.py extract "<BF1942_DIR>/Mods/bf1942/Archives/ai.rfa" out/
python3 tools/con.py "<BF1942_DIR>" bf1942 Wake    # load a level, print summary
python3 tools/sm.py "<BF1942_DIR>" bf1942 standardMesh/willy_hul_m1 out/jeep   # OBJ + textures
python3 -m unittest discover tools     # tests
python3 tools/check_docs.py            # documentation checks
```

## Engine

C++17, CMake, SDL2, OpenGL 3.3 (libepoxy), Jolt Physics (downloaded by CMake on first configure). On Debian/Ubuntu: `apt install cmake g++ git libsdl2-dev libepoxy-dev`.

```
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --play 2      # play conquest (team 2, assault kit)
build/obf_mapview "<BF1942_DIR>" bf1942 Wake                     # free camera
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy  # a vehicle in front of the camera
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600 # headless: 10 minutes of conquest, printed
```

Controls when playing:

| | |
|---|---|
| Move, look | WASD, mouse (captured; F10 frees it), shift walks, space jumps (falling: opens the parachute) |
| Weapons | left mouse fires, R reloads, 1–9 kit weapons, G detonates explosive packs |
| Posture, view | left ctrl crouches, Z lies down, V first / third person |
| Vehicles | E gets in / out, F1–F8 change seat, WASD drives, mouse aims turrets |
| Aircraft | W / S throttle, arrows pitch and roll, A / D rudder |
| Free camera | right mouse looks, WASD / QE move, shift fast; C shows collision meshes; Esc quits |

## Roadmap

Goal: play with original Battlefield 1942 clients and servers. Next, in order ([full roadmap](docs/roadmap/roadmap.md)):

1. **Consolidate** (done) — split the viewer into a game library and a renderer; CI.
2. **Network protocol research** (one session recorded) — decode original client/server traffic (alongside the steps below).
3. **AI navigation**, **audio**, **HUD** (first parts done).
4. **Complete soldiers and weapons** (mostly done), **vehicle crews** (land and sea done, pilots open), **effects**.
5. **Fidelity** — measure the original and calibrate.
6. **Game modes**, **menus**.
7. **Networking** — our server and client, then interoperability with the original.
8. **Rendering**, **distribution** (Windows/Linux, widescreen, mods).

## Documentation

[docs/README.md](docs/README.md) — formats, architecture decisions, debt. AI assistants: start at [AGENTS.md](AGENTS.md).
