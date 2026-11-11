# Network (roadmap E0, research in progress)

Status: **one 5-minute client/server session recorded (2026-11-10). Discovery and query protocols identified; the game transport layer decoded and checked on every packet of the recording (`tools/bfnet.py`); payload messages not decoded yet.** Goal and rules: [ADR 0005](../architecture/adr/0005-network-compatibility.md).

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

### Payload (in progress, unverified)

- A bit stream read least-significant bit first. Strings are an 8-bit length and 8-bit characters at any bit offset (the server's first data packet carries the mod and server names; the client's first ones its player name and CD-key hash).
- Client data packets start with 11 constant bits (value 193) and a counter that rises by 1 per packet: its input frame (31 bits read; the bit after it is always set).
- Server data packets start with 6 bits: 4 on ordinary updates (then a 14-bit or wider tick counter), 6 on about 60 packets that come every 10 s or with game events (a reliable-message counter appears a few bits later), 2 once.
- Ordinary server updates carry three floats from bit 94: a world position (x, y, z), matching the Berlin spawn area the player walked in; 5,483 of 5,573 server packets hold one, moving at a median 0.9 m/s with one jump (a respawn, probably). Whose object it is (the player's own soldier, or the first object of a list) is open.
- Client frames and server ticks run over the same span (0–8,585 and 2,020–10,605): one game clock at about 30 Hz, the server's counting from before the client joined.

A second client with the same CD key gets 20 packets of handshake and no more until the server restarts (confirmed).

## Next

- Payload messages: the fields after the counters, the reliable messages (prefix 6), the connection packets, then object updates (`NetworkableInfo`).
- Record more sessions with the WSL relay (`tools/bfnet.py record`); this needs no elevation and works remotely. Wanted: two clients, vehicles, deaths, a map change.
