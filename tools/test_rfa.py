import os
import tempfile
import unittest

from rfa import lzo1x_decompress, read_file, safe_path


class LzoTest(unittest.TestCase):
    def test_literals_then_end(self):
        self.assertEqual(lzo1x_decompress(bytes([20]) + b"abc" + bytes([0x11, 0, 0]), 3), b"abc")

    def test_overlapping_m2_match(self):
        # "abcd" literal run, then M2 match: dist 4, len 8 -> "abcd" * 3
        stream = bytes([21]) + b"abcd" + bytes([(7 << 5) | (3 << 2), 0]) + bytes([0x11, 0, 0])
        self.assertEqual(lzo1x_decompress(stream, 12), b"abcd" * 3)

    def test_wrong_size_raises(self):
        with self.assertRaises(ValueError):
            lzo1x_decompress(bytes([20]) + b"abc" + bytes([0x11, 0, 0]), 4)


class RawFileTest(unittest.TestCase):
    def test_truncated_raw_file_raises(self):
        self.assertEqual(read_file(b"xxabcd", False, 4, 2), b"abcd")
        with self.assertRaises(ValueError):
            read_file(b"xxab", False, 4, 2)


class SafePathTest(unittest.TestCase):
    def test_rejects_escape(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertTrue(safe_path(d, "a/b.con").startswith(os.path.realpath(d)))
            with self.assertRaises(ValueError):
                safe_path(d, "../evil.con")


if __name__ == "__main__":
    unittest.main()
