import struct
import unittest

from tm import parse


def build(collision=b"", vertex_count=4):
    d = struct.pack("<3I6f", 3, 0, 8, 0, 0, 0, 1, 1, 1) + b"\x00" * 24
    name = b"texture/x"
    # group 1 empty, group 2 one triangle, group 3 empty
    d += struct.pack("<I", 0) + struct.pack("<I", 1) + struct.pack("<3I", 0, 1, len(name)) + name + struct.pack("<I", 0)
    d += struct.pack("<I", 0) + collision
    d += struct.pack("<I", vertex_count)
    d += b"".join(struct.pack("<3f3fI2f2f", i, 0, 0, 0, 1, 0, 0x80808080, 0, 0, 0, 0) for i in range(vertex_count))
    return d + struct.pack("<I3H", 3, 0, 1, 2)


class TmTest(unittest.TestCase):
    def test_plain(self):
        t = parse(build())
        self.assertEqual(t["groups"][1], [(0, 1, "texture/x")])
        self.assertEqual(t["indices"], [0, 1, 2])
        self.assertEqual(t["vertices"][3][0], 3)

    def test_skips_unsized_collision_block(self):
        t = parse(build(collision=b"\xfa\xc2\x97\xeb" + b"\x07" * 37))
        self.assertEqual(len(t["vertices"]), 4)

    def test_index_count_mismatch_rejected(self):
        d = bytearray(build())
        d[-10:-6] = struct.pack("<I", 4)  # stored index count no longer matches the groups
        with self.assertRaises(ValueError):
            parse(bytes(d))


if __name__ == "__main__":
    unittest.main()
