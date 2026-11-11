#!/usr/bin/env python3
"""Battlefield 1942 game traffic (UDP game port): transport layer reader and a recording relay. Stdlib only.

Spec: docs/formats/network.md

Usage:
  bfnet.py record <listen_port> <server_ip> <server_port> <out.jsonl> <seconds>
      UDP relay: point the client at this machine (+joinServer <this ip>:<listen_port>); every datagram both
      ways is forwarded and logged as one JSON line {t, dir: c2s|s2c, client, hex}. The log holds the player's
      name and CD-key hash: keep it out of the repository.
  bfnet.py summary <in.jsonl>
      packets per direction and type, sequence gaps, acknowledgement mismatches, client frame / server tick range
"""
import collections
import json
import select
import socket
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


def frame(direction, payload):
    """Client input frame (c2s) or server tick (s2c) carried by an ordinary data payload, else None (unverified layout:
    c2s 11 prefix bits then a 31-bit counter; s2c prefix 4 in 6 bits then the counter)."""
    r = BitReader(payload)
    try:
        if direction == "c2s":
            return r.u(31) if r.u(11) == 0b11000001 else None
        return r.u(14) if r.u(6) == 4 else None
    except ValueError:
        return None


def load(path):
    with open(path) as f:
        return [(r["t"], r["dir"], bytes.fromhex(r["hex"])) for r in map(json.loads, f)]


def summary(rows):
    out, counts = [], collections.Counter()
    last = {"c2s": None, "s2c": None}
    gaps = mismatches = data = 0
    frames = {"c2s": [], "s2c": []}
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
        f = frame(d, p["payload"])
        if f is not None:
            frames[d].append(f)
    names = {DATA: "data", PING: "ping", PONG: "pong", CONNECT_REPLY: "type 2", CONNECT_ACK: "type 4", "connect": "connect"}
    for (d, t), n in sorted(counts.items(), key=str):
        out.append(f"{d} {names.get(t, t)}: {n}")
    out.append(f"sequence gaps: {gaps}; acks behind the last packet received: {mismatches} of {data}")
    for d, label in (("c2s", "client frames"), ("s2c", "server ticks")):
        if frames[d]:
            out.append(f"{label}: {min(frames[d])}..{max(frames[d])} in {len(frames[d])} packets")
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
