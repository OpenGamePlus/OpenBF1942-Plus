import json
import os
import tempfile
import unittest

import struct

from bfnet import DATA, PING, PONG, BitReader, frame, load, parse, position, summary


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

    def test_frames_and_summary(self):
        rows = []
        for i in range(3):
            c = bits([(0b11000001, 11), (100 + i, 31)])
            s = bits([(4, 6), (2100 + i, 14)])
            rows.append({"t": i, "dir": "c2s", "client": "x", "hex": (header(i + 1, i, DATA) + bytes([len(c), 0]) + c).hex()})
            rows.append({"t": i, "dir": "s2c", "client": "x", "hex": (header(i + 1, i + 1, DATA) + bytes([len(s), 0]) + s).hex()})
        self.assertEqual(frame("c2s", bits([(0b11000001, 11), (7, 31)])), 7)
        self.assertIsNone(frame("s2c", bits([(6, 6), (7, 14)])))
        f = [struct.unpack("<I", struct.pack("<f", c))[0] for c in (1800.5, 38.75, 12.25)]
        self.assertEqual(position(bits([(4, 6), (0, 88)] + [(c, 32) for c in f])), (1800.5, 38.75, 12.25))
        self.assertIsNone(position(bits([(6, 6), (0, 88)] + [(c, 32) for c in f])))
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "s.jsonl")
            with open(path, "w") as f:
                f.write("".join(json.dumps(r) + "\n" for r in rows))
            out = summary(load(path))
        self.assertIn("sequence gaps: 0; acks behind the last packet received: 0 of 5", out)
        self.assertIn("client frames: 100..102 in 3 packets", out)
        self.assertIn("server ticks: 2100..2102 in 3 packets", out)


if __name__ == "__main__":
    unittest.main()
