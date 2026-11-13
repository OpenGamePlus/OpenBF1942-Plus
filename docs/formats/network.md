# Network (roadmap E0, research in progress)

Status: **two client/server sessions recorded (2026-11-10, 5 min; 2026-11-13, 14 min with three rounds, vehicles and deaths, plus the server's XML event log). Discovery and query protocols identified; the game transport layer decoded and checked on every packet of the recording (`tools/bfnet.py`); payload structure read from the Mac client decompilation and checked on the second recording: every action, game event and ghost section is walked to its last byte.** Goal and rules: [ADR 0005](../architecture/adr/0005-network-compatibility.md).

## Programs

| Program | Notes |
|---|---|
| `BF1942.exe` | client; the maintainer's install is a community-patched v1.61 (window title "Ver: Henk, 23 Feb. 2022") |
| `BF1942_w32ded.exe` | dedicated server; `DedicatedServer.exe` (GUI launcher) starts it with `+game <mod> +restart 1 +dedicated 1` |
| `pb/` | PunkBuster (`pbcl.dll`, `pbsv.dll`); off in the maintainer's `ServerSettings.con` (`game.serverPunkBuster 0`) |

## Observed (2026-11-10, Berlin conquest, client and server on one Windows machine)

- The server reads `Mods/<mod>/Settings/ServerSettings.con` and `MapList.con` at start; `+config` / `+mapList` on its command line were ignored. `game.serverPort` sets the game port (14567 by default; 14570 tested).
- Server sockets: UDP game port on all interfaces; on one interface address only (here the WSL virtual adapter): the GameSpy query port (`game.gameSpyPort`, 23000 when 0) and the ASE port, game port + 123 (14690, 14693). `game.gameSpyLANPort` 23005 opened no socket.
- The client's command line builds `+joinServer <ip>:<port> +isInternet 0|1` (strings in the exe). With `+joinServer` pointing at a relay, the client still reached the server **directly**, even with the server on a non-default port: it apparently finds LAN servers through the query port and connects to the address and port the server reports.
- Connecting twice with the same CD key after killing a client gives "CD KEY IN USE" until the server is restarted: the server keeps the old session.

- Second attempt: server on game port 14570 with its query on 23006, relays on 23000 (query, rewriting the reported port) and 14567 (game) on all interfaces. The client again loaded the server's level without sending a single packet to either relay: it neither queries port 23000 nor uses the `+joinServer` address.

## Third attempt (2026-11-10, maintainer present)

How it was captured:
- **Elevated `pktmon`:** it records only the client's LAN broadcasts. Client and server on one machine talk over loopback, which `pktmon` does not see.
- **What worked:** a UDP relay inside WSL. WSL is a separate virtual machine, so its address is a real network hop. The client joined with `+joinServer <WSL address>:14567`, the relay forwarded to the server on the Windows host and logged every datagram.
- **Why the earlier relays failed:** they sat on Windows addresses (a second loopback address, the query port), not on a reachable separate host.
- **Where the recording is:** it stays outside the repository, because it carries the player's name and CD-key hash.

**LAN discovery.** The client broadcasts the GameSpy (v1) query `\status\` to the UDP broadcast address, port 22000, about every second.

**Query protocol.** The server answers GameSpy queries on `game.gameSpyPort` (23000 when 0).

| Query | Answer (backslash-separated key/value pairs) |
|---|---|
| `\status\` | `gamename bfield1942`, `gamever v1.61`, `hostname`, `hostport`, `mapname`, `gametype conquest`, `numplayers`, `maxplayers`, `tickets1/2`, `password`, `dedicated`, server options (`spawn_wave_time`, `soldier_friendly_fire`, …) |
| `\players\` | per player N: `playername_N`, `keyhash_N`, `team_N`, `score_N`, `kills_N`, `deaths_N`, `ping_N`; `teamname_0/1`; ends with `\final\\queryid\<n>.1` |

**Master server (internet server list).** GameSpy 2002 SDK. **Unverified:** taken from keowu's reverse engineering of BF1942 v1.61 ([references.md](../references.md)), not yet seen in our captures. GameSpy's own master (`master.gamespy.com`) is gone. That is why the original client's internet list is empty, while servers joined by address still work.

1. The client opens TCP port 28900 on the master. The master speaks first: `\basic\\secure\<challenge>`.
2. The client sends its query: `gamename bfield1942`, `gamever`, `location 0`, `validate <response>`, `enctype 2`, `\final\`, `queryid 1.1`. Then it sends the list request for `bfield1942`. The validate response is computed from the challenge and a secret key per game held in the executable; algorithm and key are open (see Luigi Auriemma's GameSpy papers).
3. The master answers with one buffer of at most 2,047 bytes, encrypted with **enctype 2** (GameSpy's "GOA": per-session XOR keys and shuffle rounds).
   - The first byte XOR `0xEC` is non-zero. Zero means the master refused.
   - Once decrypted, the buffer holds a header (13 bytes in keowu's parser, `0x13` in his paper's text), then 6-byte entries (IPv4 address and port, network byte order), then `\final\`.
   - The ports in keowu's sample are 23000, so the entries name each server's **query port**, not its game port.
4. The client sends `\status\` over UDP to every entry. It shows the server's answer and drops servers whose `gamever` differs from its own.

How a server registers with the master (heartbeat) is not covered by that research. GameSpy v1 servers usually send UDP heartbeats to port 27900, but that is unverified for BF1942. For E2 there are two routes: a master of our own, which needs enctype 2 and the validate algorithm, or a community master that already speaks the protocol.

**CD-key hash.** `keyhash` (in `\players\` answers and in the client's first data packets) is the MD5 of the CD key, in hex; the key itself is in the registry. The duplicate-key check compares these hashes, so the same key twice on one server blocks the second player. Our recording confirms the blocking; the MD5 claim comes from keowu's paper.

**Game traffic.** UDP on the game port. Rates in the recording:

| Phase | Packets |
|---|---|
| Connect | one 84-byte connection request (first byte 01), one type-2 / type-4 exchange, then pings and the first data packets |
| Keep-alive | a ping every 3 s from each side (13 bytes), answered by a pong (17 bytes) |
| Play | client about 30 data packets/s, mostly 54 bytes; server about 20/s, mostly 72 bytes, up to 587 |
| 5 minutes in total | client 8,863 packets / 468 KB; server 5,844 / 675 KB |

### Transport (checked on all 14,707 packets of the recording)

Little-endian. Byte 0: `00` game packet, `01` connection request (84 bytes, fields not decoded).

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `00` |
| 1 | 2 | bits 0–5 sequence (+1 per data packet, wraps at 64); bits 6–11 acknowledgement: the last sequence received from the other side; bits 12–15 type |
| 3 | 4 | received bitmask: one bit per recent packet from the other side (`ffffffff` once 32 have arrived; `01`, `03`, `07` … at the start) |
| 7 | … | by type, below |

| Type | Rest of the packet |
|---|---|
| 15 data | u16 payload length (always the bytes that follow), payload |
| 7 ping | u8 0, u32 sender's clock (ms), u8 flag (1; 0 in the very first) |
| 8 pong | u8 value (unverified: 2–5 from the server, 17–72 from the client), u32 the ping's clock echoed, u32 own clock (ms), u8 flag |
| 2, 4 | once each during the connection (server 2, client 4); not decoded |
| 5 | disconnect: sent three times each way when the client quits; the rest of the packet is uninitialised memory |

Every data packet in the recording had the next sequence number (no loss on one machine); 89 of 14,163 acknowledgements lagged the latest packet sent, the ones in flight. Pings and pongs do not advance the sequence.

### Payload

Read from the symbol-named Mac client decompilation ([references.md](../references.md): `dice::ref2::io::BitStream`, `dice::bf::ClientConnection`, the stream managers) and decoded by `tools/bfnet.py` `payload`. **Checked on the 2026-11-13 recording:** all 24,223 client action payloads and all 15,924 ghost-only server payloads end in their last byte, with no decoding error; field meanings inside the sections stay unverified unless stated.

**Bit stream.** Least-significant bit first, multi-byte values little-endian. Signed integers are a sign bit then an (n − 1)-bit magnitude. Unsigned floats are an n-bit integer divided by 2ⁿ − 1, signed floats that times 2 minus 1. A unit quaternion is a sign bit for w, then x, y, z as signed floats; w is rebuilt. Strings are an 8-bit length and 8-bit characters at any bit offset.

**Vectors.** A full vector is three 32-bit floats. A compressed vector is a 2-bit mode: 0 = full vector; 1–3 = three signed integers of a width taken from an engine table (values not in the decompilation, open), times a scale, plus a base vector set by the reader (the stream's compression vector).

**Packet.** An optional u32 (only on connections with a flag set; not seen yet), then a **6-bit mask** of the stream managers that wrote into the packet, then their sections in id order:

| Id | Manager | Section |
|---|---|---|
| 0 | PlayerAction (client → server) | u4 count; if any, s32 frame of the first action; per action six u12 values and a u32 (meaning open) |
| 1 | GameEvent (reliable) | u8 count, u5 block index, a flag; per event a 7-bit event id, then the event's own fields (see [Game events](#game-events)) |
| 2 | Ghost (server → client) | u32 server tick, u8 entry count, a flag; when set the player's **control object state**; then the entries |
| 3 | Download | open |

So the client's 11 constant bits are mask 1, count 3 and the sign of the first frame: each client packet carries three actions. Server prefix 4 is ghosts only (ordinary updates), 6 is events and ghosts, 2 events only.

The server tick counts at 30 Hz from the start of each round: it reached 9,001 at the end of each 5-minute round and restarted at 0.

**Control object state** (the player's own soldier or vehicle): u12 bit length of the rest, s32, a flag, a compressed vector with scale 1 (mode 0 puts the full position at bit 94 of an ordinary update, the floats seen in the recording), then a u16 networkable id and its state (not walked; the length skips it).

**Ghost entries:** u2 kind, u16 networkable id. Kind 0: nothing more. Kind 1: a flag and a u11 bit length, then the object's update (read by its networkable; skipped by the length). Kind 3: the ghost is removed. Kind 2: invalid.

### Ghost updates (in progress)

What a kind-1 ghost entry carries, from the Mac client (`NetworkManager::setNetUpdate`, the `*Networkable::setNetUpdate` readers). `bfnet.py` decodes the soldier; other objects are still skipped by their length.

- An object is a **descriptor**: a list of unique net properties, each served by one or more networkable components (soldier, engine, firearms, rotational bundle, spring, kit, projectile, control point…). For each property the reader rewinds to the property's first bit, and every component sharing it reads from there; the next property starts where they stopped.
- Each component starts with its own state mask and reads only the fields whose bit is set. Positions are compressed vectors relative to the stream's compression vector (reset at the start of the ghost section, set to the player's position by the control object state). Their scale comes from the reader: 0.001 for soldier positions, so mode 2 reaches ±32.8 m and mode 3 ±2 m around the player.
- **Soldier** (`BFSoldierNetworkable`): u14 mask, a flag, then by mask bit. **Checked:** 180 soldier updates of the recording read exactly their declared length, and the own soldier's decoded position matches the control position (0 m at spawn) and the server log's death spot:

  | Bit | Field |
  |---|---|
  | 0x1 | position, compressed vector, scale 0.001 |
  | 0x4 | rotation, unit quaternion with 16-bit components |
  | 0x2 | velocity, compressed vector, scale 0.001 |
  | 0x200, 0x400 | two vectors, scale 0.0001 |
  | 0x8, 0x10 | signed floats, 16 bits, scaled by limits from the template |
  | 0x20, 0x40 | animation states, range 0 – (states − 1) of the animation state machine: 11 bits with retail bf1942 |
  | 0x1000 | 16 bits |
  | 0x80 | range 0 – a value of the active weapon, compared with its ammunition, probably the rounds in the magazine (5 bits in the 6 updates seen; width depends on the weapon, so not decoded yet) |
  | 0x100 | u32 |
  | 0x800 | u15 state bits |
  | 0x2000 | flag (kit part state) |

- Widths found from the update lengths (the constants are not visible in the decompilation): compressed-vector mode 2 = 16 bits per component, mode 3 = 12 bits (mode 1 not seen yet); quaternion components 16 bits. Each guess is checked against every update's u11 bit length.
- Still open: the component list of each descriptor (from the templates' networkable info) and the readers of vehicles, weapons, projectiles, control points and the managers (ids 1, 513, 514).
- In the 2026-11-13 recording, 45 networkable ids received updates: id 1 every few ticks, 513 and 514 rarely, the level's vehicles (519–535, 544) the most (up to 1,726 updates for a driven tank), grenades (546–548, 555–557) in short bursts, and the player's soldier only 116 times, since its state travels in the control object state.

### Game events

From the Mac client's `GameEvent::initEvents` (id → class) and each class's `deSerialize`, implemented in `bfnet.py` `EVENTS`. With them every data packet of the 2026-11-13 recording decodes to its last byte (24,233 client, 16,120 server), events followed by ghosts included. Fields read with `readUnsigned(min, max)` use the smallest n with 2ⁿ − 1 ≥ max − min + 1 bits (`ScoreMsg` kind: 4 bits; `ConnectionType`: 3).

"Seen" marks the meanings matched against the server's XML event log of the same session; the rest are names and layouts only.

| Id | Event | Fields | Seen |
|---|---|---|---|
| 2 | Ack | u32 | |
| 4 | Simulation | flag, f32 | once after joining |
| 5 | CreateMultipleObjects | u32 template, u16 first networkable id, u4 count | with every spawn (the kit's weapons) |
| 6 | DestroyObject | u16 networkable id | wrecks removed, round end |
| 7 | CreateObject | u32 template, u16 networkable id, flag; when set position and rotation (full vectors) | yes: the log's vehicle positions match to the centimetre |
| 8 | CreatePlayer | u3, u4, flag, u8, 4 × u16, flag, 32-byte name | client sends its name; server echoes |
| 9 | SetDefaultVehicle | u8 player, u16 networkable id | the player's soldier at each spawn |
| 10 | SetVehicle | u8 player, u16 networkable id | yes: entering a vehicle |
| 11 | ExitVehicle | flag (1 on death), u8 player | yes |
| 12 | DestroyPlayer | u8 player | |
| 13 | SetPlayerName | u8 length, name, u8 | |
| 15 | ConnectionType | range 0–6 | client: 0 at join, 5 later |
| 16 | UpdateRate | u8 | |
| 17 | CreatePlayerAck | u8 player | |
| 19 | Map | flag, u8 and u8 lengths, u8, two strings | the server's map list (level, mod) |
| 20 | Challenge | 10 bytes, s32, u8 length + mod name, 8 bytes | server's first event |
| 21 | ChallengeResponse | 73 bytes (begins with the CD-key hash in hex), 3 × u32, s32, 32 bytes, flag | client's answer |
| 22 | GameRules | f32, flag, flag, f32, u32 (300: the round time in s), u32, flag | each round |
| 23 | WelcomeMsg | u8 length, text, s8 | |
| 24 | ResetMap | u32 | new round |
| 25 | CheckSum | u8 n, n bits, u8 m, m bits, u32 | |
| 26 / 27 | ServerInfo / ServerInfo2 | three strings (game, mod, game) / server name | at join |
| 30 | FlagHolder | flag, u8 | |
| 35 | HandlePickup | u8 player, u16 networkable id | the kit at each spawn |
| 36 | GameStatus | u8 status (5: 5 bytes and a string follow) | 3 before a round, 1 playing, 2 round over |
| 37 | ControlPointCaptured | u8, u8 | |
| 38 | ReqScoutCamera | u8, u16 | |
| 39 | SpecialGame | range 0–3 | bursts of 0 every half second, meaning open |
| 40 | ChatFragment | flag; when set three flags, u8 player unless the second flag, u8 channel; then u4 length and text | |
| 41 | TimerSync | u32, u32 | regular |
| 42 | ScoreMsg | kind (range 0–10), u8 player; kinds 3 and 6 add u8 victim, u32 weapon, u8 | 1 = flag attack, 4 = death, 7 = spawn |
| 43 | PBFragment | flag; when set u32, u16 part, u16 length, data (PunkBuster) | |
| 47 | SpawnRequest | u8 team, u8 kit | yes: kits 2, 0, 3, 2, 1 were AT, Scout, Medic, AT, Assault |
| 48–50 | StatsKills / Shots / Hits | u8 player, u8 count, 10 × (u32, u16) | round end |
| 51 | Suicide | none | |
| 52 / 53 | DataBaseComplete / Ack | u16, u16 / none | at join |
| 54 | SetLevel | 64-byte level path, 64-byte game mode file, u8, u8, u16, u8, u16, u16, u8, u32, u32 | `bf1942/levels/berlin/`, `conquest.con` |
| 55 | SessionId | 64 bytes (`_ClientID_<n>`) | |
| 57 | SetTeam | u8, u8 | |
| 58 | RadioMessage | u16, flag, u8 | yes |
| 59 | TriggerObjectSpawner | u8 | |
| 60 | HitFromPos | u3, u8 | when hit, probably the damage direction |
| 61 | RemoteCommand | 9 bytes, 64 bytes | |
| 62 | ChangeKit | u8 | |

Ids 3 (Debug, s32) and 18 (Vote) are registered too. Networkable ids of the level's objects and vehicles start above 512; the template numbers are indexes into the server's template list (1741 the Soviet soldier, 2949 the T34-85 on Berlin).

A second client with the same CD key gets 20 packets of handshake and no more until the server restarts (confirmed).

## Next

- The action values; the rest of the control state; ghost readers for vehicles, weapons, projectiles, control points and managers; compressed-vector mode 1; the download section; the meaning of the unlabelled event fields.
- Record more sessions with the WSL relay (`tools/bfnet.py record`); this needs no elevation and works remotely. Wanted: two clients, vehicles, deaths, a map change.
