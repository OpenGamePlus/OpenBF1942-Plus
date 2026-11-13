#!/usr/bin/env python3
"""Battlefield 1942 game traffic (UDP game port): transport layer reader and a recording relay. Stdlib only.

Spec: docs/formats/network.md

Usage:
  bfnet.py record <listen_port> <server_ip> <server_port> <out.jsonl> <seconds>
      UDP relay: point the client at this machine (+joinServer <this ip>:<listen_port>); every datagram both
      ways is forwarded and logged as one JSON line {t, dir: c2s|s2c, client, hex}. The log holds the player's
      name and CD-key hash: keep it out of the repository.
  bfnet.py summary <in.jsonl>
      packets per direction and type, sequence gaps, acknowledgement mismatches, payload sections (stream manager
      mask), how many fully decoded payloads end in their last byte, game events by name, client frame / server tick
      range, ghost ids
"""
import collections
import json
import math
import select
import socket
import struct
import sys
import time

DATA, CONNECT_REPLY, CONNECT_ACK, DISCONNECT, PING, PONG = 15, 2, 4, 5, 7, 8  # header types (CONNECT_*: one of each, unverified)


class BitReader:
    """Least-significant bit first, across bytes (the game payloads' order)."""

    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def left(self):
        return len(self.data) * 8 - self.pos

    def u(self, n):
        if n > self.left():
            raise ValueError("bit reader: past the end")
        v = 0
        for k in range(n):
            i = self.pos + k
            v |= ((self.data[i >> 3] >> (i & 7)) & 1) << k
        self.pos += n
        return v

    def s(self, n):
        """Sign bit, then an (n - 1)-bit magnitude."""
        neg = self.u(1)
        v = self.u(n - 1)
        return -v if neg else v

    def bytes(self, n):
        return bytes(self.u(8) for _ in range(n))

    def bits(self, n):
        """n raw bits as an int (fields whose length is counted in bits)."""
        return self.u(n)

    def vec(self):
        return tuple(struct.unpack("<f", self.u(32).to_bytes(4, "little"))[0] for _ in range(3))

    def ufloat(self, n):
        return self.u(n) / ((1 << n) - 1)

    def sfloat(self, n):
        return self.ufloat(n) * 2 - 1

    def quat(self, n=16):
        """Unit quaternion (x, y, z, w): sign of w, then x, y, z as n-bit signed floats."""
        neg = self.u(1)
        x, y, z = (self.sfloat(n) for _ in range(3))
        w = math.sqrt(max(0.0, 1 - min(1.0, x * x + y * y + z * z)))
        return (x, y, z, -w if neg else w)

    def cvec(self, scale, base):
        """Compressed vector: u2 mode; 0 = full vector, else base + scale x three signed integers."""
        mode = self.u(2)
        if mode == 0:
            return self.vec()
        if mode not in CVEC_BITS:
            raise ValueError(f"compressed vector: mode {mode} width unknown")
        return tuple(base[i] + scale * self.s(CVEC_BITS[mode]) for i in range(3))

    def string(self):
        """8-bit length, then that many 8-bit characters."""
        n = self.u(8)
        return bytes(self.u(8) for _ in range(n)).decode("latin-1")


def parse(d):
    """One datagram -> dict. Raises ValueError on a malformed packet."""
    if not d:
        raise ValueError("packet: empty")
    if d[0] == 1:
        return {"kind": "connect", "raw": d[1:]}  # 84-byte connection request; fields not decoded
    if d[0] != 0 or len(d) < 7:
        raise ValueError(f"packet: unknown first byte {d[0]} or too short")
    h = d[1] | d[2] << 8
    p = {"kind": "game", "seq": h & 63, "ack": (h >> 6) & 63, "type": h >> 12,
         "ackbits": int.from_bytes(d[3:7], "little")}
    if p["type"] == DATA:
        if len(d) < 9:
            raise ValueError("data packet: too short")
        n = d[7] | d[8] << 8
        if n != len(d) - 9:
            raise ValueError(f"data packet: length {n}, {len(d) - 9} bytes follow")
        p["payload"] = d[9:]
    elif p["type"] == PING:
        if len(d) != 13:
            raise ValueError("ping: not 13 bytes")
        p["time"], p["flag"] = int.from_bytes(d[8:12], "little"), d[12]
    elif p["type"] == PONG:
        if len(d) != 17:
            raise ValueError("pong: not 17 bytes")
        p["value"], p["echo"] = d[7], int.from_bytes(d[8:12], "little")
        p["time"], p["flag"] = int.from_bytes(d[12:16], "little"), d[16]
    else:
        p["rest"] = d[7:]
    return p


CVEC_BITS = {1: 20, 2: 16, 3: 12}  # compressed-vector mode -> bits per component, fitted from update lengths
BODY_KIT_BITS = 7  # SimpleObject field 0x10: range set by the object's weapon; 7 bits on Berlin's tanks and Hanomags
ANIM_STATE_BITS = 11  # range 0..states-1 of retail bf1942's animation state machine, from update lengths
MANAGERS = ("actions", "events", "ghosts", "downloads")  # stream manager ids 0..3, in packet order


def actions(r):
    """PlayerActionManager section: u4 count, then if any a signed 32-bit first frame, then per action six u12
    values and a u32 (meaning of the values open)."""
    n = r.u(4)
    first = r.s(32) if n else None
    return {"first": first, "actions": [([r.u(12) for _ in range(6)], r.u(32)) for _ in range(n)]}


def soldier(r, base):
    """BFSoldierNetworkable update: u14 mask, a flag, then the fields whose mask bit is set."""
    m = r.u(14)
    e = {"mask": m, "flag": r.u(1)}
    if m & 0x1:
        e["position"] = r.cvec(0.001, base)
    if m & 0x4:
        e["rotation"] = r.quat()
    if m & 0x2:
        e["velocity"] = r.cvec(0.001, base)
    for bit in (0x200, 0x400):
        if m & bit:
            e[hex(bit)] = r.cvec(0.0001, base)
    for bit in (0x8, 0x10):  # 16-bit signed floats scaled by template limits (raw value kept)
        if m & bit:
            e[hex(bit)] = r.sfloat(16)
    for bit in (0x20, 0x40):
        if m & bit:
            e[hex(bit)] = r.u(ANIM_STATE_BITS)
    if m & 0x1000:
        e["0x1000"] = r.u(16)
    if m & 0x80:
        raise ValueError("soldier: kit part field width unknown")
    if m & 0x100:
        e["0x100"] = r.u(32)
    if m & 0x800:
        e["state"] = r.u(15)
    if m & 0x2000:
        e["kit_state"] = r.u(1)
    return e


def body(r, base):
    """SimpleObjectNetworkable update (a vehicle's body): u11 mask, then the fields whose bit is set."""
    m = r.u(11)
    e = {"mask": m}
    if m & 0x2:
        e["position"] = r.cvec(0.0005, base)
    if m & 0x1:
        e["rotation"] = r.quat()
    for bit, scale in ((0x4, 0.001), (0x20, 0.0001), (0x8, 0.001), (0x40, 0.0001), (0x100, 0.0001), (0x200, 0.0001)):
        if m & bit:  # 0x4 velocity; the others are open
            e[hex(bit)] = r.cvec(scale, base)
    if m & 0x80:
        e["0x80"] = r.u(1)
    if m & 0x10:
        e["0x10"] = r.u(BODY_KIT_BITS)
    if m & 0x400:
        e["0x400"] = r.u(2)
    return e


def engine(r):
    """EngineNetworkable update: u10 mask, fields by bit, then a u32 that rises by one per update."""
    m = r.u(10)
    e = {"mask": m}
    for bit in (0x1, 0x2, 0x4):
        if m & bit:
            e[hex(bit)] = r.u(1)
    if m & 0x8:
        raise ValueError("engine: gear width unknown")  # range 0..number of gears of the engine template
    for bit in (0x10, 0x20, 0x40):
        if m & bit:
            e[hex(bit)] = r.sfloat(16)  # times a template limit
    for bit in (0x80, 0x100, 0x200):
        if m & bit:
            e[hex(bit)] = r.sfloat(12) * 1.25
    e["sequence"] = r.u(32)
    return e


def vehicle(r, base, end):
    """A vehicle's update: body, engine, then the other components (springs, turrets, weapons; not decoded)."""
    e = {"body": body(r, base), "engine": engine(r)}
    e["rest_bits"] = end - r.pos
    if e["rest_bits"] < 0:
        raise ValueError("vehicle update: read past its length")
    return e


def ghosts(r, soldiers=(), vehicles=()):
    """GhostManager section: u32 server tick, u8 entry count, a flag, the player's control object state when the
    flag is set, then the entries. Each entry is a u2 kind and a u16 networkable id; kind 1 carries a flag and an
    u11 bit length of its update (skipped here), kind 3 removes the ghost, kind 2 is an error."""
    g = {"tick": r.u(32), "entries": []}
    n, has_control = r.u(8), r.u(1)
    base = (0.0, 0.0, 0.0)  # the stream's compression vector
    if has_control:
        size = r.u(12)
        end = r.pos + size
        c = {"bits": size, "frame": r.s(32), "flag": r.u(1)}
        try:
            c["position"] = base = r.cvec(1.0, base)
        except ValueError:
            pass
        r.pos = end
        g["control"] = c
    for _ in range(n):
        kind, nid = r.u(2), r.u(16)
        e = {"kind": kind, "id": nid}
        if kind == 2:
            raise ValueError("ghost entry: kind 2")
        if kind == 1:
            e["flag"], e["bits"] = r.u(1), r.u(11)
            if e["bits"] > r.left():
                raise ValueError("ghost entry: update past the end")
            end = r.pos + e["bits"]
            if nid in soldiers:
                try:
                    e["soldier"] = soldier(r, base)
                    if r.pos != end:
                        raise ValueError(f"soldier update: read {r.pos - end + e['bits']} bits of {e['bits']}")
                except ValueError as x:
                    e["soldier_error"] = str(x)
            elif nid in vehicles:
                try:
                    e["vehicle"] = vehicle(r, base, end)
                except ValueError as x:
                    e["vehicle_error"] = str(x)
            r.pos = end
        g["entries"].append(e)
    return g


# Game events by id: name and field list, read in order. Field kinds: ("u", bits), ("s", bits) sign-magnitude,
# ("b",) flag, ("raw", bytes), ("str",) u8 length then bytes, ("vec",) full vector. Events with branches have a
# function instead. From the Mac client's GameEvent::initEvents and each event's deSerialize.
def _u(n): return ("u", n)


def _range_bits(lo, hi):
    """Width BitStream::readUnsigned(min, max) uses: the smallest n with 2^n - 1 >= max - min + 1."""
    n = 0
    while (1 << n) - 1 < hi - lo + 1:
        n += 1
    return n
B, STR, VEC = ("b",), ("str",), ("vec",)


def _create_object(r):
    e = {"template": r.u(32), "id": r.u(16), "placed": r.u(1)}  # template index, networkable id
    if e["placed"]:
        e["position"], e["rotation"] = r.vec(), r.vec()
    return e


def _chat(r):
    e = {"text_part": r.u(1)}
    if e["text_part"]:
        e["flags"] = [r.u(1) for _ in range(3)]
        if not e["flags"][1]:
            e["player"] = r.u(8)
        e["channel"] = r.u(8)
    e["text"] = r.bytes(r.u(4))
    return e


def _game_status(r):
    e = {"status": r.u(8)}
    if e["status"] == 5:
        e["raw"], e["text"] = r.bytes(5), r.bytes(r.u(8))
    return e


def _score(r):
    e = {"kind": r.u(_range_bits(0, 10)), "player": r.u(8)}
    if e["kind"] in (3, 6):
        e["victim"], e["weapon"], e["extra"] = r.u(8), r.u(32), r.u(8)
    return e


def _map(r):
    e = {"flag": r.u(1)}
    n1, n2, e["value"] = r.u(8), r.u(8), r.u(8)
    e["a"], e["b"] = r.bytes(n1), r.bytes(n2)
    return e


def _checksum(r):
    a = r.bits(r.u(8))
    return {"a": a, "b": r.bits(r.u(8)), "sum": r.u(32)}


def _stats(r):
    return {"player": r.u(8), "count": r.u(8), "rows": [(r.u(32), r.u(16)) for _ in range(10)]}


EVENTS = {
    2: ("Ack", [_u(32)]), 3: ("Debug", [("s", 32)]), 4: ("Simulation", [B, ("raw", 4)]),
    5: ("CreateMultipleObjects", [_u(32), _u(16), _u(4)]), 6: ("DestroyObject", [_u(16)]),
    7: ("CreateObject", _create_object),
    8: ("CreatePlayer", [_u(3), _u(4), B, _u(8), _u(16), _u(16), _u(16), _u(16), B, ("raw", 32)]),
    9: ("SetDefaultVehicle", [_u(8), _u(16)]), 10: ("SetVehicle", [_u(8), _u(16)]), 11: ("ExitVehicle", [B, _u(8)]),
    12: ("DestroyPlayer", [_u(8)]), 13: ("SetPlayerName", [STR, _u(8)]), 14: ("SetTempCamera", [_u(16)]),
    15: ("ConnectionType", [_u(_range_bits(0, 6))]), 16: ("UpdateRate", [_u(8)]), 17: ("CreatePlayerAck", [_u(8)]),
    18: ("Vote", [_u(8)] * 5 + [("raw", 4), _u(_range_bits(0, 4)), _u(_range_bits(0, 3))]), 19: ("Map", _map),
    20: ("Challenge", [("raw", 10), ("s", 32), STR, ("raw", 8)]),
    21: ("ChallengeResponse", [("raw", 73), _u(32), _u(32), _u(32), ("s", 32), ("raw", 32), B]),
    22: ("GameRules", [("raw", 4), B, B, ("raw", 4), _u(32), _u(32), B]), 23: ("WelcomeMsg", [STR, ("s", 8)]),
    24: ("ResetMap", [_u(32)]), 25: ("CheckSum", _checksum), 26: ("ServerInfo", [STR, STR, STR]),
    27: ("ServerInfo2", [STR]), 30: ("FlagHolder", [B, _u(8)]), 35: ("HandlePickup", [_u(8), _u(16)]),
    36: ("GameStatus", _game_status), 37: ("ControlPointCaptured", [_u(8), _u(8)]),
    38: ("ReqScoutCamera", [_u(8), _u(16)]), 39: ("SpecialGame", [_u(2)]), 40: ("ChatFragment", _chat),
    41: ("TimerSync", [_u(32), _u(32)]), 42: ("ScoreMsg", _score),
    43: ("PBFragment", None), 47: ("SpawnRequest", [_u(8), _u(8)]),
    48: ("StatsKills", _stats), 49: ("StatsShots", _stats), 50: ("StatsHits", _stats), 51: ("Suicide", []),
    52: ("DataBaseComplete", [_u(16), _u(16)]), 53: ("DatabaseCompleteAck", []),
    54: ("SetLevel", [("raw", 64), ("raw", 64), _u(8), _u(8), _u(16), _u(8), _u(16), _u(16), _u(8), _u(32), _u(32)]),
    55: ("SessionId", [("raw", 64)]), 57: ("SetTeam", [_u(8), _u(8)]), 58: ("RadioMessage", [_u(16), B, _u(8)]),
    59: ("TriggerObjectSpawner", [_u(8)]), 60: ("HitFromPos", [_u(3), _u(8)]),
    61: ("RemoteCommand", [("raw", 9), ("raw", 64)]), 62: ("ChangeKit", [_u(8)]),
}


def _pb_fragment(r):
    e = {"more": r.u(1)}
    if e["more"]:
        e["id"], e["part"] = r.u(32), r.u(16)
        e["data"] = r.bytes(r.u(16))
    return e


def event(r):
    """One game event: 7-bit id, then its fields. Raises ValueError on an unknown id."""
    eid = r.u(7)
    if eid not in EVENTS:
        raise ValueError(f"game event: unknown id {eid}")
    name, spec = EVENTS[eid]
    if eid == 43:
        spec = _pb_fragment
    if callable(spec):
        return {"id": eid, "name": name, **spec(r)}
    vals = []
    for f in spec:
        kind = f[0]
        vals.append(r.u(f[1]) if kind == "u" else r.s(f[1]) if kind == "s" else r.u(1) if kind == "b" else
                    r.bytes(f[1]) if kind == "raw" else r.bytes(r.u(8)) if kind == "str" else r.vec())
    return {"id": eid, "name": name, "fields": vals}


def events(r):
    """GameEventManager section: u8 count, u5 block index, a flag, then the events."""
    n, block, flag = r.u(8), r.u(5), r.u(1)
    return {"block": block, "flag": flag, "events": [event(r) for _ in range(n)]}


def payload(direction, data, soldiers=(), vehicles=()):
    """Data payload -> {"mask", section name: decoded, "end": bit position where decoding stopped, "open": name of
    the first section not decodable yet (downloads) or None}. A 6-bit mask names the stream managers that wrote into
    the packet; their sections follow in id order. Ghost updates of the ids in `soldiers` / `vehicles` are decoded as
    such."""
    r = BitReader(data)
    out = {"mask": r.u(6), "open": None}
    for i, name in enumerate(MANAGERS):
        if not out["mask"] >> i & 1:
            continue
        if name == "actions":
            out[name] = actions(r)
        elif name == "ghosts":
            out[name] = ghosts(r, soldiers, vehicles)
        elif name == "events":
            out[name] = events(r)
        else:  # downloads: not decoded
            out["open"] = name
            break
    out["end"] = r.pos
    return out


def load(path):
    with open(path) as f:
        return [(r["t"], r["dir"], bytes.fromhex(r["hex"])) for r in map(json.loads, f)]


def summary(rows):
    out, counts = [], collections.Counter()
    last = {"c2s": None, "s2c": None}
    gaps = mismatches = data = 0
    frames = {"c2s": [], "s2c": []}
    masks, errors, tails, positions, ids = collections.Counter(), collections.Counter(), collections.Counter(), [], set()
    names, soldiers, soldier_stats = collections.Counter(), set(), collections.Counter()
    vehicles, vehicle_stats = set(), collections.Counter()
    for _, d, b in rows:
        p = parse(b)
        if p["kind"] == "connect":
            counts[(d, "connect")] += 1
            continue
        counts[(d, p["type"])] += 1
        if p["type"] != DATA:
            continue
        other = "s2c" if d == "c2s" else "c2s"
        if last[other] is not None:
            data += 1
            mismatches += p["ack"] != last[other]
        if last[d] is not None and (p["seq"] - last[d]) % 64 != 1:
            gaps += 1
        last[d] = p["seq"]
        try:
            m = payload(d, p["payload"], soldiers, vehicles - soldiers)
        except ValueError as e:
            errors[(d, str(e))] += 1
            continue
        masks[(d, m["mask"])] += 1
        if m["open"] is None:  # fully walked: what is left must be byte padding
            tails[(d, len(p["payload"]) * 8 - m["end"] < 8)] += 1
        if m.get("actions", {}).get("first") is not None:
            frames[d].append(m["actions"]["first"])
        for e in m.get("events", {}).get("events", []):
            names[(d, e["name"])] += 1
            if e["name"] == "SetDefaultVehicle":  # the player's soldier
                soldiers.add(e["fields"][1])
            elif e["name"] == "SetVehicle":  # a vehicle the player entered
                vehicles.add(e["fields"][1])
            elif e["name"] == "DestroyObject":
                soldiers.discard(e["fields"][0])
                vehicles.discard(e["fields"][0])
        for e in m.get("ghosts", {}).get("entries", []):
            if "soldier" in e:
                soldier_stats["decoded"] += 1
            elif "soldier_error" in e:
                soldier_stats[e["soldier_error"]] += 1
            elif "vehicle" in e:
                vehicle_stats["decoded"] += 1
            elif "vehicle_error" in e:
                vehicle_stats[e["vehicle_error"]] += 1
        if "ghosts" in m:
            frames[d].append(m["ghosts"]["tick"])
            ids.update(e["id"] for e in m["ghosts"]["entries"])
            if "position" in m["ghosts"].get("control", {}):
                positions.append(m["ghosts"]["control"]["position"])
    labels = {DATA: "data", PING: "ping", PONG: "pong", CONNECT_REPLY: "type 2", CONNECT_ACK: "type 4", DISCONNECT: "disconnect",
              "connect": "connect"}
    for (d, t), n in sorted(counts.items(), key=str):
        out.append(f"{d} {labels.get(t, t)}: {n}")
    out.append(f"sequence gaps: {gaps}; acks behind the last packet received: {mismatches} of {data}")
    for (d, mask), n in sorted(masks.items()):
        out.append(f"{d} mask {mask} ({'+'.join(x for i, x in enumerate(MANAGERS) if mask >> i & 1) or 'none'}): {n}")
    for (d, ok), n in sorted(tails.items()):
        out.append(f"{d} fully decoded payloads ending {'in the last byte' if ok else 'EARLY'}: {n}")
    for (d, e), n in sorted(errors.items()):
        out.append(f"{d} decode errors '{e}': {n}")
    for d, label in (("c2s", "client frames"), ("s2c", "server ticks")):
        if frames[d]:
            out.append(f"{label}: {min(frames[d])}..{max(frames[d])} in {len(frames[d])} packets")
    for d in ("c2s", "s2c"):
        ev = sorted((k[1], n) for k, n in names.items() if k[0] == d)
        if ev:
            out.append(f"{d} events: " + ", ".join(f"{k} {n}" for k, n in ev))
    if vehicle_stats:
        out.append("vehicle updates: " + ", ".join(f"{k} {n}" for k, n in sorted(vehicle_stats.items())))
    if soldier_stats:
        out.append("soldier updates: " + ", ".join(f"{k} {n}" for k, n in sorted(soldier_stats.items())))
    if ids:
        out.append(f"ghost ids: {len(ids)} distinct, {min(ids)}..{max(ids)}")
    if positions:
        lo = [min(v[i] for v in positions) for i in range(3)]
        hi = [max(v[i] for v in positions) for i in range(3)]
        out.append(f"control object positions: {len(positions)}, x {lo[0]:.0f}..{hi[0]:.0f} y {lo[1]:.0f}..{hi[1]:.0f} z {lo[2]:.0f}..{hi[2]:.0f}")
    return out


def record(listen_port, server_ip, server_port, path, seconds):
    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind(("", listen_port))  # all interfaces
    ups = {}  # client address -> socket to the server
    t0 = time.time()
    with open(path, "w") as out:
        while time.time() < t0 + seconds:
            ready, _, _ = select.select([front] + list(ups.values()), [], [], 0.5)
            for s in ready:
                if s is front:
                    data, addr = front.recvfrom(65535)
                    if addr not in ups:
                        ups[addr] = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        ups[addr].connect((server_ip, server_port))
                    ups[addr].send(data)
                    d = "c2s"
                else:
                    data = s.recv(65535)
                    addr = next(a for a, v in ups.items() if v is s)
                    front.sendto(data, addr)
                    d = "s2c"
                out.write(json.dumps({"t": round(time.time() - t0, 4), "dir": d, "client": f"{addr[0]}:{addr[1]}",
                                      "hex": data.hex()}) + "\n")
            out.flush()


def main(argv):
    if len(argv) == 7 and argv[1] == "record":
        record(int(argv[2]), argv[3], int(argv[4]), argv[5], float(argv[6]))
    elif len(argv) == 3 and argv[1] == "summary":
        print("\n".join(summary(load(argv[2]))))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
