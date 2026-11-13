# Roadmap

Goal: a modern engine that plays Battlefield 1942 from the user's retail install and **interoperates with the original game: original clients on our server, our client on original servers** ([ADR 0005](../architecture/adr/0005-network-compatibility.md)). Order set 2026-11-10; each step closes when its exit check passes and its docs are updated.

## Where we are

| Original step | State |
|---|---|
| 1–4 Archives, scripts, meshes, terrain + map objects | done, verified on all 78 levels |
| 5 Skinned meshes, animation | done: soldiers, held weapons, first person, fire / reload, seated soldiers; tracks open |
| 6 Physics, vehicles | cars, tanks, aircraft, boats, moving parts, crews and seats; handling uncalibrated |
| 7 Gameplay | conquest against bots with full kits, explosions, supply depots, postures, parachutes, bot crews, sound and HUD; look compared with the original client |
| 8 Networking | research: two sessions recorded; query, master server (from research), transport, game events and 97 % of object updates decoded ([network.md](../formats/network.md)) |

## Order and why

Network compatibility makes fidelity a requirement, not a polish step: an original client runs its own simulation and trusts the server for object state, so our rules, object identities and replicated values have to match Refractor's. The protocol is undocumented and the riskiest item, so its research starts right after the code is reorganised, and its findings shape the game layer built in B.

Third-party research feeds several steps ([references.md](../references.md)). Plan set 2026-11-13: E0 reads the payload through the symbol-named Linux server and Mac client decompilations; then small fidelity checks (terrain material 7 is outside the world, COL02 collision, non-power-of-two textures, textures resolved by file name); the GameSpy master server goes into the network spec for E2; the rest waits for its step (C5 `.lsb` and `.rs` flags, D server event logs, C4 `MemeFile` menus).

| # | Phase | Steps | Exit check |
|---|---|---|---|
| 1 | **A. Consolidate** (done 2026-11-10) | A1 split `mapview.cpp` (~1,500 lines: rendering, game, AI, demos) into a game library (world: soldiers, vehicles, projectiles, rules, AI) with no SDL/GL, a renderer, and thin executables; A2 CI on GitHub (build + all tests); A3 headless game runner (the `--sim` checks as a tool) | same outputs as today on the 78-level sweeps; CI green |
| 2 | **E0. Protocol research** (runs alongside 3–5; payload decoded: actions, 51 game events, 97 % of object updates; open: object layouts built from templates, the server's template numbering) | capture traffic between an original client and server on the maintainer's machine; transport, connection handshake, packet framing, compression; map the `NetworkableInfo` templates (`setNetworkableInfo WillyBodyInfo`, `ControlPointInfo`, …) to replicated fields; game-server query protocol | a network format spec (docs/formats, new) with a parser that decodes recorded sessions (tool + tests on synthetic packets) |
| 3 | **B1. AI navigation** (level 0 used; bots leave blocked cells by a visible exit; city captures still rare) | read `<Map>/Pathfinding/` maps; bots route around obstacles | bots reach their flag on all levels; firefights per minute up sharply |
| 4 | **C1. Audio** (weapons, impacts, explosions, vehicle parts, shells in flight, footsteps and ambient play; radio and voices open; [ADR 0006](../architecture/adr/0006-audio.md)) | `.wav` playback, `.ssc` sound scripts on templates (engines, weapons, impacts, radio) | every vehicle and weapon of bf1942 makes its sound |
| 5 | **C2. HUD and text** (`.font` text, crosshair, health, tickets, feed; `.dif`, ammo, minimap open) | fonts (`.dif`, `.font`), crosshair, hit points, ammo, tickets, minimap, kill messages | window-title HUD retired |
| 6 | **B2. Complete soldier and weapons** (magazines, reload, supply depots, explosion radius, kit weapons, grenades, mines, explosive packs, crouch / prone, parachutes, held weapons, first person done) | magazines and reload, secondary weapons and switching, grenades, explosions with radius, held weapon, first person, crouch / prone, ladders, swimming, parachutes | every kit usable as in the original |
| 7 | **B3. Vehicle crews** (seats for the player, bot drivers and gunners on land, seated soldiers, landing craft, ship spawns done; pilots open) | entry points, driver / gunner / passenger seats (`SeatObject`, sub-`PlayerControlObject`s), gunner weapons, bots driving and flying | a bot-crewed tank fights; players switch seats |
| 8 | **C3. Effects** | emitters and particles (`e_*` templates): muzzle flash, explosions, smoke, dust, wakes, damage fires | destroyed vehicles burn |
| 9 | **D. Fidelity** | measure the original (speeds, turn rates, climb, damage, capture times, ticket bleed, respawn) and calibrate every value marked unverified in [debt.md](debt.md) | side-by-side measurements within tolerance, documented |
| 10 | **B4. Game modes** | conquest rules verified, CTF, team deathmatch, co-op; round end and map rotation | each mode playable on its levels |
| 11 | **C4. Menus** | decode `MemeFile 2.0` menus or rebuild them: map, team, kit, options, server browser | start a game without command-line options |
| 12 | **E1. Our server, our client** | authoritative server, client prediction, replication modelled on E0's object state | two of our clients play conquest together |
| 13 | **E2. Original interoperability** | our server accepts original clients; our client joins original servers; server query for browsers | a mixed game: original and our clients on each kind of server |
| 14 | **C5. Rendering** | distance LOD, `.lsb` shadow bits, dynamic shadows, water, terrain detail, performance on the largest maps | 60 fps on a mid-range GPU on every level |
| 15 | **F. Distribution and "Plus"** | Windows and Linux builds, launcher pointing at the install, widescreen / high resolution / FOV, key binding, mod stacks (Desert Combat, Forgotten Hope) | release build plays bf1942, XPack1, XPack2 and one community mod |

## Maintainer actions needed

- **E0, D:** run the original game (client and dedicated server) for captures and measurements; the agent prepares exact scripts of what to record.
- Decisions along the way: library choices for audio and networking (ADRs), whether to rebuild or decode menus.

## Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Protocol obfuscation, encryption or anti-cheat | E2 blocked or partial | research first (E0); fall back to our own protocol (E1) |
| Simulation divergence from the original | original clients desync on our server | D before E2; replay captured sessions through our simulation |
| Data-driven behaviour we misread | wrong handling, rules | every reading marked unverified until measured (D) |
