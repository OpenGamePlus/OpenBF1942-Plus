import json
import os
import tempfile
import unittest

import struct

from bfnet import DATA, PING, PONG, BitReader, load, parse, payload, summary


def header(seq, ack, kind, ackbits=0xFFFFFFFF):
    h = seq | ack << 6 | kind << 12
    return bytes([0, h & 255, h >> 8]) + ackbits.to_bytes(4, "little")


def bits(fields):
    """[(value, width)] -> bytes, least-significant bit first."""
    v = n = 0
    for value, width in fields:
        v |= value << n
        n += width
    return v.to_bytes((n + 7) // 8, "little")


class BfnetTest(unittest.TestCase):
    def test_data(self):
        p = parse(header(5, 9, DATA) + bytes([3, 0]) + b"abc")
        self.assertEqual((p["seq"], p["ack"], p["type"], p["payload"]), (5, 9, DATA, b"abc"))
        with self.assertRaises(ValueError):
            parse(header(5, 9, DATA) + bytes([4, 0]) + b"abc")  # length does not match

    def test_ping_pong(self):
        p = parse(header(1, 2, PING) + bytes([0]) + (3032).to_bytes(4, "little") + bytes([1]))
        self.assertEqual((p["time"], p["flag"]), (3032, 1))
        p = parse(header(1, 2, PONG) + bytes([5]) + (3032).to_bytes(4, "little") + (81303).to_bytes(4, "little") + b"\1")
        self.assertEqual((p["value"], p["echo"], p["time"]), (5, 3032, 81303))
        with self.assertRaises(ValueError):
            parse(header(1, 2, PING) + bytes(5))

    def test_bits(self):
        r = BitReader(bits([(5, 3), (6, 8)] + [(ord(c), 8) for c in "bf1942"] + [(1, 1)]))
        self.assertEqual(r.u(3), 5)  # a 3-bit field, then a string not aligned to a byte
        self.assertEqual(r.string(), "bf1942")
        self.assertEqual(r.u(1), 1)
        with self.assertRaises(ValueError):
            r.u(8)

    def test_signed(self):
        r = BitReader(bits([(1, 1), (37, 31), (0, 1), (5, 4)]))
        self.assertEqual((r.s(32), r.s(5)), (-37, 5))  # sign bit, then magnitude

    def test_payload(self):
        # client: mask 1 (actions), 3 actions from frame 900; the first 11 bits are the old "193" prefix
        acts = [(1, 6), (3, 4), (0, 1), (900, 31)]
        for i in range(3):
            acts += [(i * 6 + k, 12) for k in range(6)] + [(1000 + i, 32)]
        m = payload("c2s", bits(acts))
        self.assertEqual(bits(acts)[0] | (bits(acts)[1] & 7) << 8, 0b11000001)
        self.assertEqual((m["mask"], m["actions"]["first"], m["open"]), (1, 900, None))
        self.assertEqual(m["actions"]["actions"][2], ([12, 13, 14, 15, 16, 17], 1002))
        # server: mask 4 (ghosts), control state with a full vector at bit 94, an update, a removal, a bare entry
        f = [struct.unpack("<I", struct.pack("<f", c))[0] for c in (1800.5, 38.75, 12.25)]
        control = [(0, 32), (1, 1), (0, 2)] + [(c, 32) for c in f] + [(0, 5)]
        g = [(4, 6), (2100, 32), (3, 8), (1, 1), (sum(w for _, w in control), 12)] + control
        g += [(1, 2), (77, 16), (0, 1), (9, 11), (0x1FF, 9), (3, 2), (78, 16), (0, 2), (79, 16)]
        m = payload("s2c", bits(g))
        self.assertEqual(m["ghosts"]["control"]["position"], (1800.5, 38.75, 12.25))
        self.assertEqual([(e["kind"], e["id"]) for e in m["ghosts"]["entries"]], [(1, 77), (3, 78), (0, 79)])
        self.assertEqual((m["ghosts"]["tick"], m["end"]), (2100, sum(w for _, w in g)))
        # events then ghosts: SpawnRequest (team 2, kit 3), ScoreMsg kind 4 (4-bit range-coded kind), CreateObject
        ev = [(2, 6), (3, 8), (0, 5), (0, 1), (47, 7), (2, 8), (3, 8), (42, 7), (4, 4), (0, 8)]
        ev += [(7, 7), (2949, 32), (532, 16), (1, 1)] + [(c, 32) for c in f] * 2
        m = payload("s2c", bits(ev))
        e = m["events"]["events"]
        self.assertEqual([x["name"] for x in e], ["SpawnRequest", "ScoreMsg", "CreateObject"])
        self.assertEqual((e[0]["fields"], e[1]["kind"], e[2]["template"], e[2]["id"]), ([2, 3], 4, 2949, 532))
        self.assertEqual(e[2]["position"], (1800.5, 38.75, 12.25))
        self.assertEqual((m["open"], m["end"]), (None, sum(w for _, w in ev)))
        with self.assertRaises(ValueError):
            payload("s2c", bits([(2, 6), (1, 8), (0, 6), (1, 7)]))  # event id 1 is not registered
        with self.assertRaises(ValueError):
            payload("s2c", bits([(4, 6), (0, 32), (1, 8), (0, 1), (2, 2), (1, 16)]))  # kind 2 is an error

    def test_soldier(self):
        # control state at (1800, 40, 1900); soldier 536: mask 0x5 = position (mode 3, 12 bits) + rotation (16 bits)
        f = [struct.unpack("<I", struct.pack("<f", c))[0] for c in (1800.0, 40.0, 1900.0)]
        control = [(0, 32), (0, 1), (0, 2)] + [(c, 32) for c in f]
        upd = [(0x5, 14), (0, 1), (3, 2), (0, 1), (1500, 11), (0, 1), (0, 11), (1, 1), (250, 11), (0, 1)]
        upd += [(65535, 16), (32767, 16), (32767, 16)]
        g = [(4, 6), (10, 32), (1, 8), (1, 1), (sum(w for _, w in control), 12)] + control
        g += [(1, 2), (536, 16), (0, 1), (sum(w for _, w in upd), 11)] + upd
        e = payload("s2c", bits(g), {536: "soldier"})["ghosts"]["entries"][0]
        x, y, z = e["update"]["position"]
        self.assertAlmostEqual(x, 1801.5, places=3)
        self.assertAlmostEqual(y, 40.0, places=3)
        self.assertAlmostEqual(z, 1899.75, places=3)
        self.assertAlmostEqual(e["update"]["rotation"][0], 1.0, places=3)
        self.assertNotIn("update", payload("s2c", bits(g))["ghosts"]["entries"][0])  # class unknown: skipped

    def test_vehicle(self):
        # no control state: base (0, 0, 0); body mask 0x2 = position mode 1 (20 bits, scale 0.0005);
        # engine mask 0x200 (12-bit float), sequence 7; then 22 bits of other components
        body_ = [(0x2, 11), (1, 2), (0, 1), (200000, 19), (0, 1), (80000, 19), (0, 1), (400000, 19)]
        eng = [(0x200, 10), (4095, 12), (7, 32)]
        upd = body_ + eng + [(0, 22)]
        g = [(4, 6), (10, 32), (1, 8), (0, 1), (1, 2), (531, 16), (0, 1), (sum(w for _, w in upd), 11)] + upd
        v = payload("s2c", bits(g), {531: "vehicle"})["ghosts"]["entries"][0]["update"]
        self.assertEqual([round(c, 3) for c in v["body"]["position"]], [100.0, 40.0, 200.0])
        self.assertEqual((v["engine"]["sequence"], v["rest_bits"]), (7, 22))
        self.assertAlmostEqual(v["engine"]["0x200"], 1.25)

    def test_small_readers(self):
        g = [(4, 6), (10, 32), (3, 8), (0, 1)]
        g += [(1, 2), (515, 16), (0, 1), (7, 11), (3, 2), (1, 1), (3, 3), (1, 1)]  # flag: value -3, flag 1
        g += [(1, 2), (540, 16), (0, 1), (8, 11), (8, 4), (5, 4)]  # kit: mask 0x8, value 5
        proj = [(0x3, 8), (3, 2)] + [(0, 1), (100, 11)] * 3 + [(0, 1)] + [(2047, 12)] * 3
        g += [(1, 2), (546, 16), (0, 1), (sum(w for _, w in proj), 11)] + proj
        u = [e["update"] for e in payload("s2c", bits(g), {515: "control_point", 540: "kit", 546: "projectile"})["ghosts"]["entries"]]
        self.assertEqual((u[0]["value"], u[0]["flag"], u[1]["0x8"]), (-3, 1, 5))
        self.assertEqual([round(c, 2) for c in u[2]["position"]], [1.0, 1.0, 1.0])
        bad = [(4, 6), (10, 32), (1, 8), (0, 1), (1, 2), (515, 16), (0, 1), (5, 11), (0, 5)]
        self.assertIn("read 2 bits of 5", payload("s2c", bits(bad), {515: "control_point"})["ghosts"]["entries"][0]["error"])

    def test_summary(self):
        rows = []
        for i in range(3):
            c = bits([(1, 6), (1, 4), (0, 1), (100 + i, 31)] + [(0, 12)] * 6 + [(0, 32)])
            s = bits([(4, 6), (2100 + i, 32), (0, 8), (0, 1)])
            rows.append({"t": i, "dir": "c2s", "client": "x", "hex": (header(i + 1, i, DATA) + bytes([len(c), 0]) + c).hex()})
            rows.append({"t": i, "dir": "s2c", "client": "x", "hex": (header(i + 1, i + 1, DATA) + bytes([len(s), 0]) + s).hex()})
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "s.jsonl")
            with open(path, "w") as f:
                f.write("".join(json.dumps(r) + "\n" for r in rows))
            out = summary(load(path))
        self.assertIn("sequence gaps: 0; acks behind the last packet received: 0 of 5", out)
        self.assertIn("client frames: 100..102 in 3 packets", out)
        self.assertIn("server ticks: 2100..2102 in 3 packets", out)
        self.assertIn("s2c mask 4 (ghosts): 3", out)
        self.assertIn("c2s fully decoded payloads ending in the last byte: 3", out)


if __name__ == "__main__":
    unittest.main()
