import struct
import unittest

from anim import parse_baf, parse_ske, parse_skn, pose, skin


def name(s):
    b = s.encode() + b"\0"
    return struct.pack("<H", len(b)) + b


IDENTITY_T = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)


def ske_bytes():
    # Root at origin; child 1 m along x.
    return (struct.pack("<II", 1, 2) + name("Root") + struct.pack("<h12f", -1, *IDENTITY_T)
            + name("Tip") + struct.pack("<h12f", 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0))


def baf_bytes(prec=10):
    # One bone "Tip", two frames: 90 deg about z, then identity; position x = 2 m via RLE.
    q = 23170  # 0.7071 * 32767
    chans = [
        b"\x03\x00" + bytes([2, 0]) + struct.pack("<2h", 0, 0),                # qx literal
        b"\x02\x00" + bytes([0x82, 0]) + struct.pack("<h", 0),                 # qy repeated (2 words)
        b"\x03\x00" + bytes([2, 0]) + struct.pack("<2h", q, 0),                # qz literal
        b"\x03\x00" + bytes([2, 0]) + struct.pack("<2h", q, 32767),            # qw literal
        b"\x02\x00" + bytes([0x82, 0]) + struct.pack("<h", 2 << prec),         # px repeated
        b"\x02\x00" + bytes([0x82, 0]) + struct.pack("<h", 0),
        b"\x02\x00" + bytes([0x82, 0]) + struct.pack("<h", 0),
    ]
    words = 3 + 2 + 3 + 3 + 2 + 2 + 2
    return struct.pack("<IH", 3, 1) + name("Tip") + struct.pack("<IB", 2, prec) + struct.pack("<H", words) + b"".join(chans)


def skn_bytes():
    # One vertex at (1, 0.5, 0) skinned to Tip with offset (0, 0.5, 0); bone table maps 0 -> "Tip".
    return (struct.pack("<II", 1, 1) + struct.pack("<3fB", 1, 0.5, 0, 1) + struct.pack("<Hf3f", 0, 1.0, 0, 0.5, 0)
            + struct.pack("<H", 1) + name("Tip"))


class AnimTest(unittest.TestCase):
    def test_parse_and_rest_pose(self):
        ske = parse_ske(ske_bytes())
        self.assertEqual([(n, p) for n, p, _ in ske], [("Root", -1), ("Tip", 0)])
        recs, names = parse_skn(skn_bytes())
        self.assertEqual(names, ["Tip"])
        out = skin([(1, 0.5, 0)], recs, names, pose(ske))
        self.assertAlmostEqual(out[0][0], 1)
        self.assertAlmostEqual(out[0][1], 0.5)

    def test_animation_frames(self):
        names, nframes, bones = parse_baf(baf_bytes())
        self.assertEqual((names, nframes), (["Tip"], 2))
        self.assertAlmostEqual(bones[0][4][0], 2.0)  # position decoded with 2^prec
        ske = parse_ske(ske_bytes())
        recs, skn_names = parse_skn(skn_bytes())
        # Frame 0: Tip at x = 2, rotated 90 deg about z, so offset (0, 0.5, 0) -> (-0.5, 0, 0).
        p = skin([(1, 0.5, 0)], recs, skn_names, pose(ske, [(names, nframes, bones)], 0))[0]
        self.assertAlmostEqual(p[0], 1.5, places=3)
        self.assertAlmostEqual(p[1], 0, places=3)

    def test_truncated_animation_rejected(self):
        with self.assertRaises(Exception):
            parse_baf(baf_bytes()[:-2])


if __name__ == "__main__":
    unittest.main()
