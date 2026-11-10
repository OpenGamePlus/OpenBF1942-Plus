# Network (roadmap E0, research in progress)

Status: **no packet captured yet; observations of the original programs only.** Goal and rules: [ADR 0005](../architecture/adr/0005-network-compatibility.md).

## Programs

| Program | Notes |
|---|---|
| `BF1942.exe` | client; the maintainer's install is a community-patched v1.61 (window title "Ver: Henk, 23 Feb. 2022") |
| `BF1942_w32ded.exe` | dedicated server; `DedicatedServer.exe` (GUI launcher) starts it with `+game <mod> +restart 1 +dedicated 1` |
| `pb/` | PunkBuster (`pbcl.dll`, `pbsv.dll`); off in the maintainer's `ServerSettings.con` (`game.serverPunkBuster 0`) |

## Observed (2026-11-10, Berlin conquest, client and server on one Windows machine)

- The server reads `Mods/<mod>/Settings/ServerSettings.con` and `MapList.con` at start; `+config` / `+mapList` on its command line were ignored. `game.serverPort` sets the game port (14567 by default; 14570 tested).
- Server sockets: UDP game port on all interfaces; UDP 23000 and a second UDP port (14690, 14693) on one interface address only (here the WSL virtual adapter).
- The client's command line builds `+joinServer <ip>:<port> +isInternet 0|1` (strings in the exe). With `+joinServer` pointing at a relay, the client still reached the server **directly**, even with the server on a non-default port: it apparently finds LAN servers through the query port and connects to the address and port the server reports.
- Connecting twice with the same CD key after killing a client gives "CD KEY IN USE" until the server is restarted: the server keeps the old session.

## Capture plan

Without administrator rights (no `pktmon`, no Npcap) a UDP relay must sit on every path the client uses: answer or forward the LAN query (port 23000), rewriting the reported game port to the relay's, then relay the game port. Alternative: one elevated capture session (`pktmon` or Wireshark/Npcap), which needs the maintainer at the machine.
