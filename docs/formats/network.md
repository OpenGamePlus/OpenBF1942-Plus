# Network (roadmap E0, research in progress)

Status: **one 5-minute client/server session recorded (2026-11-10). Discovery and query protocols identified; the game transport layer decoded and checked on every packet of the recording (`tools/bfnet.py`); payload structure read from the Mac client decompilation (stream manager sections, ghost entries), not yet checked on traffic.** Goal and rules: [ADR 0005](../architecture/adr/0005-network-compatibility.md).

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

Every data packet in the recording had the next sequence number (no loss on one machine); 89 of 14,163 acknowledgements lagged the latest packet sent, the ones in flight. Pings and pongs do not advance the sequence.

### Payload

Read from the symbol-named Mac client decompilation ([references.md](../references.md): `dice::ref2::io::BitStream`, `dice::bf::ClientConnection`, the stream managers) and decoded by `tools/bfnet.py` `payload`. **Unverified as a whole:** the recording of 2026-11-10 is gone, so the full walk has not run on real traffic yet. What that recording did show fits it exactly: the server's prefixes 4, 6 and 2, the client's constant 11 bits (193) and the floats at bit 94.

**Bit stream.** Least-significant bit first, multi-byte values little-endian. Signed integers are a sign bit then an (n − 1)-bit magnitude. Unsigned floats are an n-bit integer divided by 2ⁿ − 1, signed floats that times 2 minus 1. A unit quaternion is a sign bit for w, then x, y, z as signed floats; w is rebuilt. Strings are an 8-bit length and 8-bit characters at any bit offset.

**Vectors.** A full vector is three 32-bit floats. A compressed vector is a 2-bit mode: 0 = full vector; 1–3 = three signed integers of a width taken from an engine table (values not in the decompilation, open), times a scale, plus a base vector set by the reader (the stream's compression vector).

**Packet.** An optional u32 (only on connections with a flag set; not seen yet), then a **6-bit mask** of the stream managers that wrote into the packet, then their sections in id order:

| Id | Manager | Section |
|---|---|---|
| 0 | PlayerAction (client → server) | u4 count; if any, s32 frame of the first action; per action six u12 values and a u32 (meaning open) |
| 1 | GameEvent (reliable) | u8 count, u5 block index, a flag; per event a 7-bit event id, then the event's own fields (one reader per event type; open) |
| 2 | Ghost (server → client) | u32 server tick, u8 entry count, a flag; when set the player's **control object state**; then the entries |
| 3 | Download | open |

So the client's 11 constant bits are mask 1, count 3 and the sign of the first frame: each client packet carries three actions. Server prefix 4 is ghosts only (ordinary updates), 6 is events and ghosts, 2 events only.

**Control object state** (the player's own soldier or vehicle): u12 bit length of the rest, s32, a flag, a compressed vector with scale 1 (mode 0 puts the full position at bit 94 of an ordinary update, the floats seen in the recording), then a u16 networkable id and its state (not walked; the length skips it).

**Ghost entries:** u2 kind, u16 networkable id. Kind 0: nothing more. Kind 1: a flag and a u11 bit length, then the object's update (read by its networkable; skipped by the length). Kind 3: the ghost is removed. Kind 2: invalid.

A second client with the same CD key gets 20 packets of handshake and no more until the server restarts (confirmed).

## Next

- Record a new session and run `bfnet.py summary` on it: every ghost-only server payload must end in its last byte.
- Game events (prefix 6): the per-event readers (`GameEventMaker` classes); the action values; the control state and per-object updates (`NetworkableInfo`); the compressed-vector bit tables.
- Record more sessions with the WSL relay (`tools/bfnet.py record`); this needs no elevation and works remotely. Wanted: two clients, vehicles, deaths, a map change.
