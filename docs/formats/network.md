# Network (roadmap E0, research in progress)

Status: **one 5-minute client/server session recorded (2026-11-10). Discovery and query protocols identified; game packets recorded but not decoded.** Goal and rules: [ADR 0005](../architecture/adr/0005-network-compatibility.md).

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

**Game traffic.** UDP on the game port.

| Phase | Packets |
|---|---|
| Connect | client 84 bytes starting `01 42 13 00`, then 13 / 8 / 17-byte exchanges both ways; server 74 bytes, client 56 and 134 bytes (text-like, bit-packed) |
| Keep-alive | 13- and 17-byte packets every 3 s, both ways |
| Play | client about 30 packets/s, mostly 54 bytes; server about 20 packets/s, mostly 72 bytes, up to 587 |
| 5 minutes in total | client 8,863 packets / 468 KB; server 5,844 / 675 KB |

The first byte is 00 (data) or 01 (connection); a counter in the following bits rises packet by packet. The payloads look bit-packed (unverified).

A second client with the same CD key gets 20 packets of handshake and no more until the server restarts (confirmed).

## Next

- Decode the game packets offline from the recording: bit layout of the header, sequence and acknowledgement, the handshake fields, then object updates (`NetworkableInfo`).
- Record more sessions with the WSL relay; this needs no elevation and works remotely. Wanted: two clients, vehicles, deaths, a map change.
