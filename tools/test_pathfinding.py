import struct
import unittest

from pathfinding import FULL, parse


def build(markers_and_tiles, a=1, b=5):
    d = struct.pack("<8I", a, a, b, 0, 0, 2, 0, FULL)
    for m, words in markers_and_tiles:
        d += struct.pack("<I", m) + b"".join(struct.pack("<I", w) for w in words)
    return d


class PathfindingTest(unittest.TestCase):
    def test_tiles(self):
        # 2 x 2 tiles of 32 x 32 cells: free, blocked, one full tile with row 0 cell 0 and row 31 cell 31 set, free
        full = [0] * 32
        full[0], full[31] = 1, 1 << 31
        n, g = parse(build([(0, []), (1, []), (FULL, full), (0, [])]))
        self.assertEqual(n, 64)
        self.assertEqual(g[0], 0)
        self.assertEqual(g[32], 1)                  # tile 1 (x 1, z 0) blocked
        self.assertEqual(g[32 * 64], 1)             # tile 2 (x 0, z 1), row 0 cell 0
        self.assertEqual(g[63 * 64 + 31], 1)        # its row 31, cell 31
        self.assertEqual(g[63 * 64 + 30], 0)
        self.assertEqual(sum(g), 32 * 32 + 2)

    def test_rejects(self):
        with self.assertRaises(ValueError):
            parse(build([(0, []), (5, []), (0, []), (0, [])]))       # unknown marker
        with self.assertRaises(ValueError):
            parse(build([(0, []), (FULL, [0] * 3)]))                  # truncated tile
        with self.assertRaises(ValueError):
            parse(build([(0, [])] * 4) + b"\0")                       # trailing data


if __name__ == "__main__":
    unittest.main()
