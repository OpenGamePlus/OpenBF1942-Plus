import struct
import unittest

from sm import TRIANGLE_LIST, TRIANGLE_STRIP, parse, parse_rs, to_obj, triangles


def material_header(name, prim, nv, ni):
    return struct.pack("<I", len(name)) + name + struct.pack("<9I", 0, 0, 0, prim, 0x411, 32, nv, ni, 0)


def build(version, collision_blocks, prim=TRIANGLE_LIST, indices=(0, 1, 2)):
    d = struct.pack("<II6f", version, 0, 0, 0, 0, 1, 1, 1)
    if version == 10:
        d += b"\x00"
    d += struct.pack("<I", len(collision_blocks))
    for block in collision_blocks:
        d += struct.pack("<I", len(block)) + block
    verts = b"".join(struct.pack("<8f", x, 0, 0, 0, 1, 0, x, 0) for x in (0, 1, 2))
    d += struct.pack("<II", 1, 1) + material_header(b"Mat0", prim, 3, len(indices))
    d += verts + struct.pack(f"<{len(indices)}H", *indices)
    return d + b"\x00" * 62  # retail files end with an undecoded footer


class SmTest(unittest.TestCase):
    def test_v9_without_collision(self):
        m = parse(build(9, []))
        self.assertEqual(m["lods"][0][0]["indices"], [0, 1, 2])
        self.assertEqual(m["lods"][0][0]["vertices"][2][0], 2)

    def test_v10_with_two_collision_blocks(self):
        m = parse(build(10, [b"abc", b"defgh"]))
        self.assertEqual(m["collision"], [b"abc", b"defgh"])
        self.assertEqual(m["lods"][0][0]["name"], "Mat0")

    def test_bad_index_rejected(self):
        with self.assertRaises(ValueError):
            parse(build(9, [], indices=(0, 1, 7)))

    def test_strip_flips_winding_and_skips_degenerates(self):
        m = {"prim": TRIANGLE_STRIP, "indices": [0, 1, 2, 3, 3, 4]}
        self.assertEqual(triangles(m), [(0, 1, 2), (2, 1, 3)])

    def test_obj_mirrors_z(self):
        obj = to_obj(parse(build(9, [])))
        self.assertIn("f 1/1/1 3/3/3 2/2/2", obj)

    def test_parse_rs(self):
        rs = 'subshader "Mat0" "StandardMesh/Default"\n{\n\tlighting true;\n\ttexture "texture/x_s";\n}\n'
        self.assertEqual(parse_rs(rs)["mat0"]["texture"], "texture/x_s")


if __name__ == "__main__":
    unittest.main()
