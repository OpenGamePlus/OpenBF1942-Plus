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
      mask), how many fully decoded payloads end in their last byte, client frame / server tick range, ghost ids
"""
import collections
import json
import select
import socket
import struct
import sys
import time

DATA, CONNECT_REPLY, CONNECT_ACK, PING, PONG = 15, 2, 4, 7, 8  # header types (CONNECT_*: one of each, unverified)


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


MANAGERS = ("actions", "events", "ghosts", "downloads")  # stream manager ids 0..3, in packet order


def actions(r):
    """PlayerActionManager section: u4 count, then if any a signed 32-bit first frame, then per action six u12
    values and a u32 (meaning of the values open)."""
    n = r.u(4)
    first = r.s(32) if n else None
    return {"first": first, "actions": [([r.u(12) for _ in range(6)], r.u(32)) for _ in range(n)]}


def ghosts(r):
    """GhostManager section: u32 server tick, u8 entry count, a flag, the player's control object state when the
    flag is set, then the entries. Each entry is a u2 kind and a u16 networkable id; kind 1 carries a flag and an
    u11 bit length of its update (skipped here), kind 3 removes the ghost, kind 2 is an error."""
    g = {"tick": r.u(32), "entries": []}
    n, has_control = r.u(8), r.u(1)
    if has_control:
        size = r.u(12)
        end = r.pos + size
        c = {"bits": size, "frame": r.s(32), "flag": r.u(1), "mode": r.u(2)}
        if c["mode"] == 0:  # full vector; other modes use bit widths held in engine tables (open)
            c["position"] = tuple(struct.unpack("<f", r.u(32).to_bytes(4, "little"))[0] for _ in range(3))
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
            r.pos += e["bits"]
        g["entries"].append(e)
    return g


def payload(direction, data):
    """Data payload -> {"mask", section name: decoded, "end": bit position where decoding stopped, "open": name of
    the first section not decodable yet (game events, downloads) or None}. A 6-bit mask names the stream managers
    that wrote into the packet; their sections follow in id order."""
    r = BitReader(data)
    out = {"mask": r.u(6), "open": None}
    for i, name in enumerate(MANAGERS):
        if not out["mask"] >> i & 1:
            continue
        if name == "actions":
            out[name] = actions(r)
        elif name == "ghosts":
            out[name] = ghosts(r)
        else:  # events: u8 count, u5 block, flag, then per event a 7-bit id and its own fields
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
            m = payload(d, p["payload"])
        except ValueError as e:
            errors[(d, str(e))] += 1
            continue
        masks[(d, m["mask"])] += 1
        if m["open"] is None:  # fully walked: what is left must be byte padding
            tails[(d, len(p["payload"]) * 8 - m["end"] < 8)] += 1
        if m.get("actions", {}).get("first") is not None:
            frames[d].append(m["actions"]["first"])
        if "ghosts" in m:
            frames[d].append(m["ghosts"]["tick"])
            ids.update(e["id"] for e in m["ghosts"]["entries"])
            if "position" in m["ghosts"].get("control", {}):
                positions.append(m["ghosts"]["control"]["position"])
    names = {DATA: "data", PING: "ping", PONG: "pong", CONNECT_REPLY: "type 2", CONNECT_ACK: "type 4", "connect": "connect"}
    for (d, t), n in sorted(counts.items(), key=str):
        out.append(f"{d} {names.get(t, t)}: {n}")
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
