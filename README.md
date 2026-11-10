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
| 4. Terrain + map objects | done — C++ viewer renders every map with buildings and trees |
| 5. Skinned meshes + animations | in progress — soldiers and flags animate in the viewer (`--soldier USSoldier`) |
| 6. Physics + vehicles | in progress — cars, tanks, aircraft and boats in the viewer (`--drive Willy`, `Sherman`, `Spitfire`, `LCVP`); handling not yet calibrated |
| 7. Gameplay | started — a conquest game against bots (`--play 2 --kit 1`): flags, tickets, win; drive any parked vehicle (E), fire (left mouse); retail armour and damage tables |
| 8 | not started (see [roadmap](#roadmap)) |

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
build/obf_mapview "<BF1942_DIR>" bf1942 Wake      # right mouse: look, WASD/QE: move, shift: fast
build/obf_sim "<BF1942_DIR>" bf1942 El_Alamein --play 2 --sim 600   # headless: 10 minutes of conquest, printed
build/obf_mapview "<BF1942_DIR>" bf1942 El_Alamein --drive Willy   # WASD drives, right mouse aims turrets, left mouse fires; aircraft: W/S throttle, arrows, A/D rudder
```

## Roadmap

Goal: play with original Battlefield 1942 clients and servers. Next, in order ([full roadmap](docs/roadmap/roadmap.md)):

1. **Consolidate** (done) — split the viewer into a game library and a renderer; CI.
2. **Network protocol research** — record and decode original client/server traffic (alongside the steps below).
3. **AI navigation** (first part done), **audio** (weapons and engines play), **HUD**.
4. **Complete soldiers and weapons**, **vehicle crews**, **effects**.
5. **Fidelity** — measure the original and calibrate.
6. **Game modes**, **menus**.
7. **Networking** — our server and client, then interoperability with the original.
8. **Rendering**, **distribution** (Windows/Linux, widescreen, mods).

## Documentation

[docs/README.md](docs/README.md) — formats, architecture decisions, debt. AI assistants: start at [AGENTS.md](AGENTS.md).
